#include "Instruction.Create.h"

struct OpCodeHandler* EncoderInternal_OpCodeHandlers_Handlers = (struct OpCodeHandler*)NULL; // CodeEnumCount
struct Op* ops_legacy = NULL;
struct Op* ops_vex = NULL;
struct Op* ops_evex = NULL;
struct Op* ops_xop = NULL;
struct Op* ops_mvex = NULL;
struct Op* ops_d3now = NULL;

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

void OpCodeHandler_init(struct OpCodeHandler** o,
	enum EncFlags2 encFlags2,
	enum EncFlags3 encFlags3,
	bool isSpecialInstr,
	struct Op* operands,
	unsigned int operands_length,
	bool(*TryConvertToDisp8N)(struct Encoder* encoder, struct OpCodeHandler* handler, struct Instruction* instruction, int displ, signed char* compressedValue),
	unsigned int (*GetOpCode)(struct OpCodeHandler* self, enum EncFlags2 encFlags2),
	void (*EncodeParam)(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction))
{
	(*o)->EncFlags2 = encFlags2;
	(*o)->EncFlags3 = encFlags3;
	(*o)->IsSpecialInstr = isSpecialInstr;
	(*o)->Operands = operands;
	(*o)->Operands_Length = operands_length;
	(*o)->GetOpCode = GetOpCode;
	(*o)->Encode = EncodeParam;
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

struct Op* EVEXHandler_CreateOps(enum EncFlags1 encFlags1, int* operands_length)
{
	if (ops_evex == NULL)
	{
		ops_evex = Operands_EVexOps();
	}

	int op0 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_EVEX_Op0Shift) & (unsigned int)EFLAGS_EVEX_OpMask);
	int op1 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_EVEX_Op1Shift) & (unsigned int)EFLAGS_EVEX_OpMask);
	int op2 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_EVEX_Op2Shift) & (unsigned int)EFLAGS_EVEX_OpMask);
	int op3 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_EVEX_Op3Shift) & (unsigned int)EFLAGS_EVEX_OpMask);

	if (op3 != 0)
	{
		//Debug.Assert(op0 != 0 && op1 != 0 && op2 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 4);
		w[0] = ops_evex[op0 - 1];
		w[1] = ops_evex[op1 - 1];
		w[2] = ops_evex[op2 - 1];
		w[3] = ops_evex[op3 - 1];
		*operands_length = 4;
		return w;
	}
	if (op2 != 0)
	{
		//Debug.Assert(op0 != 0 && op1 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 3);
		w[0] = ops_evex[op0 - 1];
		w[1] = ops_evex[op1 - 1];
		w[2] = ops_evex[op2 - 1];
		*operands_length = 3;
		return w;
	}
	if (op1 != 0)
	{
		//Debug.Assert(op0 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 2);
		w[0] = ops_evex[op0 - 1];
		w[1] = ops_evex[op1 - 1];
		*operands_length = 2;
		return w;
	}
	if (op0 != 0)
	{
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 1);
		w[0] = ops_evex[op0 - 1];
		*operands_length = 1;
		return w;
	}

	return NULL;
}

struct Op* XopHandler_CreateOps(enum EncFlags1 encFlags1, int* operands_length)
{
	if (ops_xop == NULL)
	{
		ops_xop = Operands_XopOps();
	}

	int op0 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_XOP_Op0Shift) & (unsigned int)EFLAGS_XOP_OpMask);
	int op1 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_XOP_Op1Shift) & (unsigned int)EFLAGS_XOP_OpMask);
	int op2 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_XOP_Op2Shift) & (unsigned int)EFLAGS_XOP_OpMask);
	int op3 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_XOP_Op3Shift) & (unsigned int)EFLAGS_XOP_OpMask);

	if (op3 != 0)
	{
		//Debug.Assert(op0 != 0 && op1 != 0 && op2 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 4);
		w[0] = ops_xop[op0 - 1];
		w[1] = ops_xop[op1 - 1];
		w[2] = ops_xop[op2 - 1];
		w[3] = ops_xop[op3 - 1];
		*operands_length = 4;
		return w;
	}
	if (op2 != 0)
	{
		//Debug.Assert(op0 != 0 && op1 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 3);
		w[0] = ops_xop[op0 - 1];
		w[1] = ops_xop[op1 - 1];
		w[2] = ops_xop[op2 - 1];
		*operands_length = 3;
		return w;
	}
	if (op1 != 0)
	{
		//Debug.Assert(op0 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 2);
		w[0] = ops_xop[op0 - 1];
		w[1] = ops_xop[op1 - 1];
		*operands_length = 2;
		return w;
	}
	if (op0 != 0)
	{
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 1);
		w[0] = ops_xop[op0 - 1];
		*operands_length = 1;
		return w;
	}

	return NULL;
}

struct Op* MvexHandler_CreateOps(enum EncFlags1 encFlags1, int* operands_length)
{
	if (ops_mvex == NULL)
	{
		ops_mvex = Operands_MVEXOps();
	}

	int op0 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_MVEX_Op0Shift) & (unsigned int)EFLAGS_MVEX_OpMask);
	int op1 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_MVEX_Op1Shift) & (unsigned int)EFLAGS_MVEX_OpMask);
	int op2 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_MVEX_Op2Shift) & (unsigned int)EFLAGS_MVEX_OpMask);
	int op3 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_MVEX_Op3Shift) & (unsigned int)EFLAGS_MVEX_OpMask);

	if (op3 != 0)
	{
		//Debug.Assert(op0 != 0 && op1 != 0 && op2 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 4);
		w[0] = ops_mvex[op0 - 1];
		w[1] = ops_mvex[op1 - 1];
		w[2] = ops_mvex[op2 - 1];
		w[3] = ops_mvex[op3 - 1];
		*operands_length = 4;
		return w;
	}
	if (op2 != 0)
	{
		//Debug.Assert(op0 != 0 && op1 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 3);
		w[0] = ops_mvex[op0 - 1];
		w[1] = ops_mvex[op1 - 1];
		w[2] = ops_mvex[op2 - 1];
		*operands_length = 3;
		return w;
	}
	if (op1 != 0)
	{
		//Debug.Assert(op0 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 2);
		w[0] = ops_mvex[op0 - 1];
		w[1] = ops_mvex[op1 - 1];
		*operands_length = 2;
		return w;
	}
	if (op0 != 0)
	{
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 1);
		w[0] = ops_mvex[op0 - 1];
		*operands_length = 1;
		return w;
	}

	return NULL;
}

struct Op* D3nowHandler_CreateOps(enum EncFlags1 encFlags1, int* operands_length)
{
	if (ops_d3now == NULL)
	{
		ops_d3now = Operands_D3NowOps();
	}
	return ops_d3now;
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
	enum WBit wbit;
	enum LBit lbit;

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
				handler->Encode = &LegacyHandler_Encode;
				handler->handler_conf = LegacyHandler;
				//handler = new LegacyHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);
			}
			break;
		case EncodingKind_VEX:
			handler->Operands_Length = 38;
			handler->table = ((unsigned int)encFlags2 >> (int)EFLAGS2_TableShift) & (unsigned int)EFLAGS2_TableMask;
			wbit = (enum WBit)(((unsigned int)encFlags2 >> (int)EFLAGS2_WBitShift) & (unsigned int)EFLAGS2_WBitMask);
			lbit = (enum LBit)(((unsigned int)encFlags2 >> (int)EFLAGS2_LBitShift) & (int)EFLAGS2_LBitMask);

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

			handler->tupleType = (enum TupleType)(((unsigned int)encFlags3Data >> (int)EFLAGS3_TupleTypeShift) & (unsigned int)EFLAGS3_TupleTypeMask);
			handler->table = ((unsigned int)encFlags2 >> (int)EFLAGS2_TableShift) & (unsigned int)EFLAGS2_TableMask;
			//Static.Assert((int)MandatoryPrefixByte.None == 0 ? 0 : -1);
			//Static.Assert((int)MandatoryPrefixByte.P66 == 1 ? 0 : -1);
			//Static.Assert((int)MandatoryPrefixByte.PF3 == 2 ? 0 : -1);
			//Static.Assert((int)MandatoryPrefixByte.PF2 == 3 ? 0 : -1);
			handler->p1Bits = 4 | (((unsigned int)encFlags2 >> (int)EFLAGS2_MandatoryPrefixShift) & (unsigned int)EFLAGS2_MandatoryPrefixMask);
			handler->wbit = (enum WBit)(((unsigned int)encFlags2 >> (int)EFLAGS2_WBitShift) & (unsigned int)EFLAGS2_WBitMask);
			if (handler->wbit == WBit_W1)
			{
				handler->p1Bits |= 0x80;
			}
			switch ((enum LBit)(((unsigned int)encFlags2 >> (int)EFLAGS2_LBitShift) & (int)EFLAGS2_LBitMask))
			{
			case LBit_LIG:
				handler->llBits = 0 << 5;
				handler->mask_LL = 3 << 5;
				break;
			case LBit_L0:
			case LBit_LZ:
			case LBit_L128:
				handler->llBits = 0 << 5;
				break;
			case LBit_L1:
			case LBit_L256:
				handler->llBits = 1 << 5;
				break;
			case LBit_L512:
				handler->llBits = 2 << 5;
				break;
			default:
				//throw new InvalidOperationException();
				break;
			}
			if (handler->wbit == WBit_WIG)
			{
				handler->mask_W |= 0x80;
			}

			operands = EVEXHandler_CreateOps(encFlags1, &operands_length);

			OpCodeHandler_init(&handler, encFlags2, encFlags3Data, false, operands, operands_length, &EVEXHandler_TryConvertToDisp8N, &OpCodeHandler_GetOpCode, &EVEXHandler_Encode);

			handler->handler_conf = EvexHandler;
			//handler = new EvexHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);
			break;

		case EncodingKind_XOP:
			handler->Operands_Length = 18;

			//Static.Assert((int)XopOpCodeTable.MAP8 == 0 ? 0 : -1);
			//Static.Assert((int)XopOpCodeTable.MAP9 == 1 ? 0 : -1);
			//Static.Assert((int)XopOpCodeTable.MAP10 == 2 ? 0 : -1);
			handler->table = 8 + (((unsigned int)encFlags2 >> (int)EFLAGS2_TableShift) & (unsigned int)EFLAGS2_TableMask);
			//Debug.Assert(table == 8 || table == 9 || table == 10);
			switch ((enum LBit)(((unsigned int)encFlags2 >> (int)EFLAGS2_LBitShift) & (int)EFLAGS2_LBitMask))
			{
			case LBit_L1:
			case LBit_L256:
				handler->lastByte = 4;
				break;
			}
			wbit = (enum WBit)(((unsigned int)encFlags2 >> (int)EFLAGS2_WBitShift) & (unsigned int)EFLAGS2_WBitMask);
			if (wbit == WBit_W1)
			{
				handler->lastByte |= 0x80;
			}
			handler->lastByte |= ((unsigned int)encFlags2 >> (int)EFLAGS2_MandatoryPrefixShift) & (unsigned int)EFLAGS2_MandatoryPrefixMask;

			operands = XopHandler_CreateOps(encFlags1, &operands_length);

			OpCodeHandler_init(&handler, encFlags2, encFlags3Data, false, operands, operands_length, NULL, &OpCodeHandler_GetOpCode, &XopHandler_Encode);

			handler->handler_conf = XopHandler;
			//handler = new XopHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);
			break;

		case EncodingKind_D3NOW:
			handler->Operands_Length = 2;

			handler->immediate = OpCodeHandler_GetOpCode(handler, encFlags2);
			
			//Debug.Assert(immediate <= byte.MaxValue);

			operands = D3nowHandler_CreateOps(encFlags1, &operands_length);

			OpCodeHandler_init(&handler, (enum EncFlags2)(((unsigned int)encFlags2 & ~(0xFFFF << (int)EFLAGS2_OpCodeShift)) | (0x000F << (int)EFLAGS2_OpCodeShift)), encFlags3Data, false, operands, operands_length, NULL, &OpCodeHandler_GetOpCode, &D3nowHandler_Encode);

			handler->handler_conf = D3nowHandler;
			//handler = new D3nowHandler((enum EncFlags2)encFlags2[i], encFlags3);
			break;

		case EncodingKind_MVEX:
			handler->Operands_Length = 8;

			handler->table = ((unsigned int)encFlags2 >> (int)EFLAGS2_TableShift) & (unsigned int)EFLAGS2_TableMask;
	/*		Static.Assert((int)MandatoryPrefixByte.None == 0 ? 0 : -1);
			Static.Assert((int)MandatoryPrefixByte.P66 == 1 ? 0 : -1);
			Static.Assert((int)MandatoryPrefixByte.PF3 == 2 ? 0 : -1);
			Static.Assert((int)MandatoryPrefixByte.PF2 == 3 ? 0 : -1);*/
			handler->p1Bits = ((unsigned int)encFlags2 >> (int)EFLAGS2_MandatoryPrefixShift) & (unsigned int)EFLAGS2_MandatoryPrefixMask;
			handler->wbit = (enum WBit)(((unsigned int)encFlags2 >> (int)EFLAGS2_WBitShift) & (unsigned int)EFLAGS2_WBitMask);
			if (wbit == WBit_W1)
			{
				handler->p1Bits |= 0x80;
			}
			if (wbit == WBit_WIG)
			{
				handler->mask_W |= 0x80;
			}
			
			operands = MvexHandler_CreateOps(encFlags1, &operands_length);

			OpCodeHandler_init(&handler, encFlags2, encFlags3Data, false, operands, operands_length, &MvexHandler_TryConvertToDisp8N, &OpCodeHandler_GetOpCode, &MvexHandler_Encode);

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
	Encoder_AddFarBranch(encoder, instruction, operand, op->size);
}
enum OpKind OpA_GetFarBranchOpKind(struct Op* op)
{
	//Debug.Assert(size == 2 || size == 4);
	return op->size == 2 ? OK_FarBranch16 : OK_FarBranch32;
}

void OpO_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	Encoder_AddAbsMem(encoder, instruction, operand);
}

void OpModRM_rm_mem_only_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (op->mustUseSib)
	{
		encoder->EncoderFlags |= EncoderFlags_MustUseSib;
	}

	Encoder_AddRegOrMemAll(encoder, instruction, operand, Register_None, Register_None, Register_None, Register_None, true, false); // allowMemOp: true, allowRegOp : false
}

void OpModRM_rm_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	Encoder_AddRegOrMemAll(encoder, instruction, operand, op->regLo, op->regHi, Register_None, Register_None, true, true); // allowMemOp: true, allowRegOp : true
}

void OpModRM_reg_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	Encoder_AddModRMRegister(encoder, instruction, operand, op->regLo, op->regHi);
}

void OpRegEmbed8_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	Encoder_AddReg(encoder, instruction, operand, op->regLo, op->regHi);
}

void OpModRM_reg_mem_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	Encoder_AddModRMRegister(encoder, instruction, operand, op->regLo, op->regHi);
	encoder->EncoderFlags |= EncoderFlags_RegIsMemory;
}

void OpModRM_rm_reg_only_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	Encoder_AddRegOrMemAll(encoder, instruction, operand, op->regLo, op->regHi, Register_None, Register_None, false, true); // allowMemOp: false, allowRegOp : true
}

void OpModRM_regF0_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (GetBitness(encoder) != 64 && GetOpKind(instruction, operand) == OK_Register && GetOpRegister(instruction, operand) >= op->regLo + 8 && GetOpRegister(instruction, operand) <= op->regLo + 15)
	{
		encoder->EncoderFlags |= EncoderFlags_PF0;
		Encoder_AddModRMRegister(encoder, instruction, operand, op->regLo + 8, op->regLo + 15);
	}
	else
	{
		Encoder_AddModRMRegister(encoder, instruction, operand, op->regLo, op->regHi);
	}
}

void OpReg_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	Verify(operand, OK_Register, GetOpKind(instruction, operand));
	VerifyRegisters(operand, op->_register, GetOpRegister(instruction, operand));
}

void OpRegSTi_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (!Verify(operand, OK_Register, GetOpKind(instruction, operand)))
	{
		return;
	}
	enum Register reg = GetOpRegister(instruction, operand);
	if (!VerifyRegister(encoder, operand, reg, Register_ST0, Register_ST7))
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
		if (!Verify(operand, op->opKind, opImmKind))
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
	if (!Verify(operand, OK_Immediate8, GetOpKind(instruction, operand)))
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
	if (!Verify(operand, OK_Immediate16, GetOpKind(instruction, operand)))
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
	if (!Verify(operand, op->opKind, opImmKind))
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
	if (!Verify(operand, OK_Immediate64, GetOpKind(instruction, operand)))
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
	Encoder_SetAddrSize(encoder, regXSize);
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
	Encoder_SetAddrSize(encoder, regYSize);
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
	Encoder_SetAddrSize(encoder, regSize);
}

void OpMRBX_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (!Verify(operand, OK_Memory, GetOpKind(instruction, operand)))
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
	Encoder_SetAddrSize(encoder, regSize);
}

void OpJ_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	Encoder_AddBranch(encoder, instruction, op->opKind, op->immSize, operand);
}

enum OpKind OpJ_GetNearBranchOpKind(struct Op* op)
{
	return op->opKind;
}

void OpJx_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	Encoder_AddBranchX(encoder, op->immSize, instruction, operand);
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
	Encoder_AddBranchDisp(encoder, op->displSize, instruction, operand);
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

void OpVsib_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	encoder->EncoderFlags |= EncoderFlags_MustUseSib;

	bool allowMemOp = true;
	bool allowRegOp = true;
	Encoder_AddRegOrMemAll(encoder, instruction, operand, Register_None, Register_None, op->regLo, op->regHi, allowMemOp, allowRegOp);
}

void OpHx_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (!Verify(operand, OK_Register, Instruction_GetOpKind(instruction, operand)))
	{
		return;
	}
	enum Register reg = GetOpRegister(instruction, operand);
	if (!VerifyRegister(encoder, operand, reg, op->regLo, op->regHi))
	{
		return;
	}
	encoder->EncoderFlags |= (enum EncoderFlags)((unsigned int)(reg - op->regLo) << (int)EncoderFlags_VvvvvShift);
}

void OpIsX_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (!Verify(operand, OK_Register, Instruction_GetOpKind(instruction, operand)))
	{
		return;
	}
	enum Register reg = GetOpRegister(instruction, operand);
	if (!VerifyRegister(encoder, operand, reg, op->regLo, op->regHi))
	{
		return;
	}
	encoder->ImmSize = ImmSize_SizeIbReg;
	encoder->Immediate = (unsigned int)(reg - op->regLo) << 4;
}

enum OpKind OpI4_GetImmediateOpKind(struct Op* op)
{
	return OK_Immediate8;
}

void OpI4_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	enum OpKind opImmKind = Instruction_GetOpKind(instruction, operand);
	if (!Verify(operand, OK_Immediate8, opImmKind))
	{
		return;
	}
	//Debug.Assert(encoder.ImmSize == ImmSize.SizeIbReg);
	//Debug.Assert((encoder.Immediate & 0xF) == 0);
	if (GetImmediate8(instruction) > 0xF)
	{
		//encoder.ErrorMessage = $"Operand {operand}: Immediate value must be 0-15, but value is 0x{instruction.Immediate8:X2}";
		return;
	}
	encoder->ImmSize = ImmSize_Size1;
	encoder->Immediate |= (unsigned int)GetImmediate8(instruction);
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

struct Op* OpIb_new(enum OpKind opKind)
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpIb;
	op->opKind = opKind;
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

struct Op* OpVsib_new(enum Register regLo, enum Register regHi)
{
	struct Op* op = Op_new();
	op->regLo = regLo;
	op->regHi = regHi;
	op->operand_type = OT_OpVsib;
	op->Encode = &OpVsib_Encode;

	return op;
}

struct Op* OpHx_new(enum Register regLo, enum Register regHi)
{
	struct Op* op = Op_new();
	op->regLo = regLo;
	op->regHi = regHi;
	op->operand_type = OT_OpHx;
	op->Encode = &OpHx_Encode;

	return op;
}

struct Op* OpIsX_new(enum Register regLo, enum Register regHi)
{
	struct Op* op = Op_new();
	op->regLo = regLo;
	op->regHi = regHi;
	op->operand_type = OT_OpIsX;
	op->Encode = &OpIsX_Encode;

	return op;
}

struct Op* OpI4_new()
{
	struct Op* op = Op_new();
	op->operand_type = OP_OpI4;
	op->Encode = &OpI4_Encode;
	op->GetImmediateOpKind = &OpI4_GetImmediateOpKind;
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

// Op Tables.
struct Op* Operands_VexOps()
{
	struct Op* operands = (struct Op*)malloc(sizeof(struct Op) * 38);
	operands[0] = *OpModRM_rm_mem_only_new(false);
	operands[1] = *OpVsib_new(Register_XMM0, Register_XMM15);
	operands[2] = *OpVsib_new(Register_XMM0, Register_XMM15);
	operands[3] = *OpVsib_new(Register_YMM0, Register_YMM15);
	operands[4] = *OpVsib_new(Register_YMM0, Register_YMM15);
	operands[5] = *OpModRM_rm_new(Register_EAX, Register_R15D);
	operands[6] = *OpModRM_rm_new(Register_RAX, Register_R15);
	operands[7] = *OpModRM_rm_new(Register_XMM0, Register_XMM15);
	operands[8] = *OpModRM_rm_new(Register_YMM0, Register_YMM15);
	operands[9] = *OpModRM_rm_new(Register_K0, Register_K7);
	operands[10] = *OpModRM_reg_new(Register_EAX, Register_R15D);
	operands[11] = *OpModRM_rm_reg_only_new(Register_EAX, Register_R15D);
	operands[12] = *OpHx_new(Register_EAX, Register_R15D);
	operands[13] = *OpModRM_reg_new(Register_RAX, Register_R15);
	operands[14] = *OpModRM_rm_reg_only_new(Register_RAX, Register_R15);
	operands[15] = *OpHx_new(Register_RAX, Register_R15);
	operands[16] = *OpModRM_reg_new(Register_K0, Register_K7);
	operands[17] = *OpModRM_rm_reg_only_new(Register_K0, Register_K7);
	operands[18] = *OpHx_new(Register_K0, Register_K7);
	operands[19] = *OpModRM_reg_new(Register_XMM0, Register_XMM15);
	operands[20] = *OpModRM_rm_reg_only_new(Register_XMM0, Register_XMM15);
	operands[21] = *OpHx_new(Register_XMM0, Register_XMM15);
	operands[22] = *OpIsX_new(Register_XMM0, Register_XMM15);
	operands[23] = *OpIsX_new(Register_XMM0, Register_XMM15);
	operands[24] = *OpModRM_reg_new(Register_YMM0, Register_YMM15);
	operands[25] = *OpModRM_rm_reg_only_new(Register_YMM0, Register_YMM15);
	operands[26] = *OpHx_new(Register_YMM0, Register_YMM15);
	operands[27] = *OpIsX_new(Register_YMM0, Register_YMM15);
	operands[28] = *OpIsX_new(Register_YMM0, Register_YMM15);
	operands[29] = *OpI4_new();
	operands[30] = *OpIb_new(OK_Immediate8);
	operands[31] = *OprDI_new();
	operands[32] = *OpJ_new(OK_NearBranch64, 1);
	operands[33] = *OpJ_new(OK_NearBranch64, 4);
	operands[34] = *OpModRM_rm_mem_only_new(true);
	operands[35] = *OpModRM_reg_new(Register_TMM0, Register_TMM7);
	operands[36] = *OpModRM_rm_reg_only_new(Register_TMM0, Register_TMM7);
	operands[37] = *OpHx_new(Register_TMM0, Register_TMM7);
	return operands;
}

// Op Tables.
struct Op* Operands_EVexOps()
{
	struct Op* operands = (struct Op*)malloc(sizeof(struct Op) * 31);
	operands[0] = *OpModRM_rm_mem_only_new(false);
	operands[1] = *OpVsib_new(Register_XMM0, Register_XMM31);
	operands[2] = *OpVsib_new(Register_XMM0, Register_XMM31);
	operands[3] = *OpVsib_new(Register_YMM0, Register_YMM31);
	operands[4] = *OpVsib_new(Register_YMM0, Register_YMM31);
	operands[5] = *OpVsib_new(Register_ZMM0, Register_ZMM31);
	operands[6] = *OpVsib_new(Register_ZMM0, Register_ZMM31);
	operands[7] = *OpModRM_rm_new(Register_EAX, Register_R15D);
	operands[8] = *OpModRM_rm_new(Register_RAX, Register_R15);
	operands[9] = *OpModRM_rm_new(Register_XMM0, Register_XMM31);
	operands[10] = *OpModRM_rm_new(Register_YMM0, Register_YMM31);
	operands[11] = *OpModRM_rm_new(Register_ZMM0, Register_ZMM31);
	operands[12] = *OpModRM_reg_new(Register_EAX, Register_R15D);
	operands[13] = *OpModRM_rm_reg_only_new(Register_EAX, Register_R15D);
	operands[14] = *OpModRM_reg_new(Register_RAX, Register_R15);
	operands[15] = *OpModRM_rm_reg_only_new(Register_RAX, Register_R15);
	operands[16] = *OpModRM_reg_new(Register_K0, Register_K7);
	operands[17] = *OpModRM_reg_new(Register_K0, Register_K7);
	operands[18] = *OpModRM_rm_reg_only_new(Register_K0, Register_K7);
	operands[19] = *OpModRM_reg_new(Register_XMM0, Register_XMM31);
	operands[20] = *OpModRM_rm_reg_only_new(Register_XMM0, Register_XMM31);
	operands[21] = *OpHx_new(Register_XMM0, Register_XMM31);
	operands[22] = *OpHx_new(Register_XMM0, Register_XMM31);
	operands[23] = *OpModRM_reg_new(Register_YMM0, Register_YMM31);
	operands[24] = *OpModRM_rm_reg_only_new(Register_YMM0, Register_YMM31);
	operands[25] = *OpHx_new(Register_YMM0, Register_YMM31);
	operands[26] = *OpModRM_reg_new(Register_ZMM0, Register_ZMM31);
	operands[27] = *OpModRM_rm_reg_only_new(Register_ZMM0, Register_ZMM31);
	operands[28] = *OpHx_new(Register_ZMM0, Register_ZMM31);
	operands[29] = *OpHx_new(Register_ZMM0, Register_ZMM31);
	operands[30] = *OpIb_new(OK_Immediate8);

	return operands;
}

// Op Tables.
struct Op* Operands_XopOps()
{
	struct Op* operands = (struct Op*)malloc(sizeof(struct Op) * 18);
	operands[0] = *OpModRM_rm_new(Register_EAX, Register_R15D);
	operands[1] = *OpModRM_rm_new(Register_RAX, Register_R15);
	operands[2] = *OpModRM_rm_new(Register_XMM0, Register_XMM15);
	operands[3] = *OpModRM_rm_new(Register_YMM0, Register_YMM15);
	operands[4] = *OpModRM_reg_new(Register_EAX, Register_R15D);
	operands[5] = *OpModRM_rm_reg_only_new(Register_EAX, Register_R15D);
	operands[6] = *OpHx_new(Register_EAX, Register_R15D);
	operands[7] = *OpModRM_reg_new(Register_RAX, Register_R15);
	operands[8] = *OpModRM_rm_reg_only_new(Register_RAX, Register_R15);
	operands[9] = *OpHx_new(Register_RAX, Register_R15);
	operands[10] = *OpModRM_reg_new(Register_XMM0, Register_XMM15);
	operands[11] = *OpHx_new(Register_XMM0, Register_XMM15);
	operands[12] = *OpIsX_new(Register_XMM0, Register_XMM15);
	operands[13] = *OpModRM_reg_new(Register_YMM0, Register_YMM15);
	operands[14] = *OpHx_new(Register_YMM0, Register_YMM15);
	operands[15] = *OpIsX_new(Register_YMM0, Register_YMM15);
	operands[16] = *OpIb_new(OK_Immediate8);
	operands[17] = *OpId_new(OK_Immediate32);
	return operands;
}

// Op Tables.
struct Op* Operands_MVEXOps()
{
	struct Op* operands = (struct Op*)malloc(sizeof(struct Op) * 8);
	operands[0] = *OpModRM_rm_mem_only_new(false);
	operands[1] = *OpVsib_new(Register_ZMM0, Register_ZMM31);
	operands[2] = *OpModRM_rm_new(Register_ZMM0, Register_ZMM31);
	operands[3] = *OpModRM_reg_new(Register_K0, Register_K7);
	operands[4] = *OpHx_new(Register_K0, Register_K7);
	operands[5] = *OpModRM_reg_new(Register_ZMM0, Register_ZMM31);
	operands[6] = *OpHx_new(Register_ZMM0, Register_ZMM31);
	operands[7] = *OpIb_new(OK_Immediate8);
	return operands;
}

// Op Tables.
struct Op* Operands_D3NowOps()
{
	struct Op* operands = (struct Op*)malloc(sizeof(struct Op) * 18);
	operands[0] = *OpModRM_reg_new(Register_MM0, Register_MM7);
	operands[1] = *OpModRM_rm_new(Register_MM0, Register_MM7);
	return operands;
}

enum OpKind GetImmediateOpKind(enum Code code, int operand) 
{
	int index = (int)code;

	if ((unsigned int)code >= (unsigned int)4936)
	{
		//throw new ArgumentOutOfRangeException(nameof(code));

		return (enum OpKind)0;
	}

	OpCodeHandlers_init(code);
	struct OpCodeHandler handler = EncoderInternal_OpCodeHandlers_Handlers[index];

	//auto handlers = EncoderInternal_OpCodeHandlers_Handlers;

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
	Instruction_SetDeclareByteValue(instruction, 0, b0);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);
	Instruction_SetDeclareByteValue(instruction, 3, b3);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);
	Instruction_SetDeclareByteValue(instruction, 3, b3);
	Instruction_SetDeclareByteValue(instruction, 4, b4);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);
	Instruction_SetDeclareByteValue(instruction, 3, b3);
	Instruction_SetDeclareByteValue(instruction, 4, b4);
	Instruction_SetDeclareByteValue(instruction, 5, b5);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);
	Instruction_SetDeclareByteValue(instruction, 3, b3);
	Instruction_SetDeclareByteValue(instruction, 4, b4);
	Instruction_SetDeclareByteValue(instruction, 5, b5);
	Instruction_SetDeclareByteValue(instruction, 6, b6);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);
	Instruction_SetDeclareByteValue(instruction, 3, b3);
	Instruction_SetDeclareByteValue(instruction, 4, b4);
	Instruction_SetDeclareByteValue(instruction, 5, b5);
	Instruction_SetDeclareByteValue(instruction, 6, b6);
	Instruction_SetDeclareByteValue(instruction, 7, b7);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);
	Instruction_SetDeclareByteValue(instruction, 3, b3);
	Instruction_SetDeclareByteValue(instruction, 4, b4);
	Instruction_SetDeclareByteValue(instruction, 5, b5);
	Instruction_SetDeclareByteValue(instruction, 6, b6);
	Instruction_SetDeclareByteValue(instruction, 7, b7);
	Instruction_SetDeclareByteValue(instruction, 8, b8);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);
	Instruction_SetDeclareByteValue(instruction, 3, b3);
	Instruction_SetDeclareByteValue(instruction, 4, b4);
	Instruction_SetDeclareByteValue(instruction, 5, b5);
	Instruction_SetDeclareByteValue(instruction, 6, b6);
	Instruction_SetDeclareByteValue(instruction, 7, b7);
	Instruction_SetDeclareByteValue(instruction, 8, b8);
	Instruction_SetDeclareByteValue(instruction, 9, b9);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);
	Instruction_SetDeclareByteValue(instruction, 3, b3);
	Instruction_SetDeclareByteValue(instruction, 4, b4);
	Instruction_SetDeclareByteValue(instruction, 5, b5);
	Instruction_SetDeclareByteValue(instruction, 6, b6);
	Instruction_SetDeclareByteValue(instruction, 7, b7);
	Instruction_SetDeclareByteValue(instruction, 8, b8);
	Instruction_SetDeclareByteValue(instruction, 9, b9);
	Instruction_SetDeclareByteValue(instruction, 10, b10);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);
	Instruction_SetDeclareByteValue(instruction, 3, b3);
	Instruction_SetDeclareByteValue(instruction, 4, b4);
	Instruction_SetDeclareByteValue(instruction, 5, b5);
	Instruction_SetDeclareByteValue(instruction, 6, b6);
	Instruction_SetDeclareByteValue(instruction, 7, b7);
	Instruction_SetDeclareByteValue(instruction, 8, b8);
	Instruction_SetDeclareByteValue(instruction, 9, b9);
	Instruction_SetDeclareByteValue(instruction, 10, b10);
	Instruction_SetDeclareByteValue(instruction, 11, b11);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);
	Instruction_SetDeclareByteValue(instruction, 3, b3);
	Instruction_SetDeclareByteValue(instruction, 4, b4);
	Instruction_SetDeclareByteValue(instruction, 5, b5);
	Instruction_SetDeclareByteValue(instruction, 6, b6);
	Instruction_SetDeclareByteValue(instruction, 7, b7);
	Instruction_SetDeclareByteValue(instruction, 8, b8);
	Instruction_SetDeclareByteValue(instruction, 9, b9);
	Instruction_SetDeclareByteValue(instruction, 10, b10);
	Instruction_SetDeclareByteValue(instruction, 11, b11);
	Instruction_SetDeclareByteValue(instruction, 12, b12);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);
	Instruction_SetDeclareByteValue(instruction, 3, b3);
	Instruction_SetDeclareByteValue(instruction, 4, b4);
	Instruction_SetDeclareByteValue(instruction, 5, b5);
	Instruction_SetDeclareByteValue(instruction, 6, b6);
	Instruction_SetDeclareByteValue(instruction, 7, b7);
	Instruction_SetDeclareByteValue(instruction, 8, b8);
	Instruction_SetDeclareByteValue(instruction, 9, b9);
	Instruction_SetDeclareByteValue(instruction, 10, b10);
	Instruction_SetDeclareByteValue(instruction, 11, b11);
	Instruction_SetDeclareByteValue(instruction, 12, b12);
	Instruction_SetDeclareByteValue(instruction, 13, b13);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);
	Instruction_SetDeclareByteValue(instruction, 3, b3);
	Instruction_SetDeclareByteValue(instruction, 4, b4);
	Instruction_SetDeclareByteValue(instruction, 5, b5);
	Instruction_SetDeclareByteValue(instruction, 6, b6);
	Instruction_SetDeclareByteValue(instruction, 7, b7);
	Instruction_SetDeclareByteValue(instruction, 8, b8);
	Instruction_SetDeclareByteValue(instruction, 9, b9);
	Instruction_SetDeclareByteValue(instruction, 10, b10);
	Instruction_SetDeclareByteValue(instruction, 11, b11);
	Instruction_SetDeclareByteValue(instruction, 12, b12);
	Instruction_SetDeclareByteValue(instruction, 13, b13);
	Instruction_SetDeclareByteValue(instruction, 14, b14);

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

	Instruction_SetDeclareByteValue(instruction, 0, b0);
	Instruction_SetDeclareByteValue(instruction, 1, b1);
	Instruction_SetDeclareByteValue(instruction, 2, b2);
	Instruction_SetDeclareByteValue(instruction, 3, b3);
	Instruction_SetDeclareByteValue(instruction, 4, b4);
	Instruction_SetDeclareByteValue(instruction, 5, b5);
	Instruction_SetDeclareByteValue(instruction, 6, b6);
	Instruction_SetDeclareByteValue(instruction, 7, b7);
	Instruction_SetDeclareByteValue(instruction, 8, b8);
	Instruction_SetDeclareByteValue(instruction, 9, b9);
	Instruction_SetDeclareByteValue(instruction, 10, b10);
	Instruction_SetDeclareByteValue(instruction, 11, b11);
	Instruction_SetDeclareByteValue(instruction, 12, b12);
	Instruction_SetDeclareByteValue(instruction, 13, b13);
	Instruction_SetDeclareByteValue(instruction, 14, b14);
	Instruction_SetDeclareByteValue(instruction, 15, b15);

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
struct Instruction* Instruction_CreateBranchFar(enum Code code, unsigned short selector, unsigned int offset)
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
/// Creates an instruction with no operands
/// </summary>
/// <param name="code">Code value</param>
struct Instruction* Instruction_CreateNoOperands(enum Code code)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, code);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

/// <summary>
/// Creates an instruction with 1 operand
/// </summary>
/// <param name="code">Code value</param>
/// <param name="memory">op0: Memory operand</param>
struct Instruction* Instruction_Create1Mem(enum Code code, struct MemoryOperand* memory)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, code);

	SetOp0Kind(instruction, OK_Memory);
	InitMemoryOperand(instruction, memory);

	//Debug.Assert(instruction.OpCount == 1);
	return instruction;
}

/// <summary>
/// Creates an instruction with 2 operands
/// </summary>
/// <param name="code">Code value</param>
/// <param name="memory">op0: Memory operand</param>
/// <param name="immediate">op1: Immediate value</param>
struct Instruction* Instruction_Create2Mem1Imm(enum Code code, struct MemoryOperand* memory, int immediate)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, code);

	SetOp0Kind(instruction, OK_Memory);
	InitMemoryOperand(instruction, memory);

	InitializeSignedImmediate(&instruction, 1, immediate);		;

	//Debug.Assert(instruction.OpCount == 2);
	return instruction;
}

/// <summary>
/// Creates an instruction with 2 operands
/// </summary>
/// <param name="code">Code value</param>
/// <param name="register">op0: Register</param>
/// <param name="immediate">op1: Immediate value</param>
struct Instruction* Instruction_Create2(enum Code code, enum Register _register, int immediate)
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
struct Instruction* Instruction_Create1(enum Code code, enum Register _register)
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
struct Instruction* Instruction_Create2Reg(enum Code code, enum Register register1, enum Register register2)
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
struct Instruction* Instruction_Create2Mem(enum Code code, enum Register register1, struct MemoryOperand* memory)
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

/// <summary>
/// Creates an instruction with 3 operands
/// </summary>
/// <param name="code">Code value</param>
/// <param name="register1">op0: Register</param>
/// <param name="register2">op1: Register</param>
/// <param name="register3">op2: Register</param>
struct Instruction* Instruction_Create3Reg(enum Code code, enum Register register1, enum Register register2, enum Register register3)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, code);

	//Static.Assert(OpKind.Register == 0 ? 0 : -1);
	//instruction.Op0Kind = OpKind.Register;
	SetOp0Register(instruction, register1);

	//Static.Assert(OpKind.Register == 0 ? 0 : -1);
	//instruction.Op1Kind = OpKind.Register;
	SetOp1Register(instruction, register2);

	//Static.Assert(OpKind.Register == 0 ? 0 : -1);
	//instruction.Op2Kind = OpKind.Register;
	SetOp2Register(instruction, register3);

	//Debug.Assert(instruction.OpCount == 3);
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
	if (!VerifyRegister(encoder, operand, reg, regLo, regHi))
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
	if (!VerifyRegister(encoder, operand, reg, regLo, regHi))
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

void Encoder_WriteByteInternal(struct Encoder* encoder, unsigned char byte_value)
{
	//TODO:
	struct Assembler* assembler = encoder->assembler;

	list_t* stream = assembler->stream_bytes;
	list_t* n;
	void* data = (void*)byte_value;

	if (stream == NULL)
	{
		stream = list_new();
		stream->data = data;
		stream->next = NULL;
		stream->prev = NULL;
	}
	else
	{
		n = list_new();
		n->data = data;
		n->next = NULL;
		n->prev = stream;

		// Append to end of stream linked list.
		stream->next = n;
		stream = n;
	}

	assembler->stream_bytes = stream;
	// stream_bytes now is a linked list of bytes.
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
	if (mvex->ehbit == MEHB_EH1)
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

struct Encoder* Encoder_new()
{
	struct Encoder* encoder = (struct Encoder*)malloc(sizeof(struct Encoder));
	encoder->assembler = NULL;
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

void Encoder_init(struct Assembler* assembler, struct Encoder* encoder, int bitness)
{
	encoder->assembler = assembler;
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
struct Encoder* Create(struct Assembler* assembler, int bitness)
{
	struct Encoder* encoder = NULL;
	switch (bitness)
	{
	case 16:
	case 32:
	case 64:
		encoder = Encoder_new();
		Encoder_init(assembler, encoder, bitness);
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

void Encoder_AddRegOrMemAll(struct Encoder* encoder, struct Instruction* instruction, int operand, enum Register regLo, enum Register regHi, enum Register vsibIndexRegLo, enum Register vsibIndexRegHi, bool allowMemOp, bool allowRegOp)
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
		if (!VerifyRegister(encoder, operand, reg, regLo, regHi))
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
	Encoder_AddRegOrMemAll(encoder, instruction, operand, regLo, regHi, Register_None, Register_None, allowMemOp, allowRegOp);
}

bool Encoder_TryConvertToDisp8N(struct Encoder* encoder, struct Instruction* instruction, int displ, signed char* compressedValue)
{
	bool(*tryConvertToDisp8N)(struct Encoder*, struct OpCodeHandler*, struct Instruction*, int, signed char*) = encoder->handler->TryConvertToDisp8N;

	if (encoder->handler->handler_conf == EvexHandler)
	{
		encoder->handler->TryConvertToDisp8N = &EVEXHandler_TryConvertToDisp8N;
	}
	else if (encoder->handler->handler_conf == MvexHandler)
	{
		encoder->handler->TryConvertToDisp8N = &MvexHandler_TryConvertToDisp8N;
	}
	else 
	{
		encoder->handler->TryConvertToDisp8N = NULL;
	}
	tryConvertToDisp8N = encoder->handler->TryConvertToDisp8N;

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
	if (baseReg != Register_None && baseReg != Register_RIP && baseReg != Register_EIP && !VerifyRegister(encoder, operand, baseReg, baseRegLo, baseRegHi))
	{
		return;
	}
	if (indexReg != Register_None && !VerifyRegister(encoder, operand, indexReg, indexRegLo, indexRegHi))
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
		struct Op op;
		for (int i = 0; i < encoder->handler->Operands_Length; i++)
		{
			op = ops[i];
			op.Encode(encoder, instruction, i, &op);
		}

		if ((encoder->handler->EncFlags3 & EFLAGS3_Fwait) != 0)
		{
			Encoder_WriteByteInternal(encoder, 0x9B);
		}

		if (encoder->handler->handler_conf == LegacyHandler)
		{
			encoder->handler->Encode = &LegacyHandler_Encode;//HACK: change this to avoid bug
		}

		
		encoder->handler->Encode(encoder->handler, encoder, instruction);

		unsigned int opCode = encoder->OpCode;
		if (!encoder->handler->Is2ByteOpCode)
		{
			Encoder_WriteByteInternal(encoder, opCode);
		}
		else
		{
			Encoder_WriteByteInternal(encoder, opCode >> 8);
			Encoder_WriteByteInternal(encoder, opCode);
		}

		if ((encoder->EncoderFlags & (EncoderFlags_ModRM | EncoderFlags_Displ)) != 0)
		{
			Encoder_WriteModRM(encoder);
		}

		if (encoder->ImmSize != ImmSize_None)
		{
			Encoder_WriteImmediate(encoder);
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