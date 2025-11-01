#include "Instruction.Create.h"

struct OpCodeHandler* EncoderInternal_OpCodeHandlers_Handlers = (struct OpCodeHandler*)NULL; // CodeEnumCount
struct Op* ops_legacy = NULL;

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
		return w;
	}
	if (op2 != 0) 
	{
		//Debug.Assert(op0 != 0 && op1 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 3);
		w[0] = ops_legacy[op0 - 1];
		w[1] = ops_legacy[op1 - 1];
		w[2] = ops_legacy[op2 - 1];
		return w;
	}
	if (op1 != 0) 
	{
		//Debug.Assert(op0 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 2);
		w[0] = ops_legacy[op0 - 1];
		w[1] = ops_legacy[op1 - 1];
		return w;
	}
	if (op0 != 0)
	{
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 1);
		w[0] = ops_legacy[op0 - 1];
		return w;
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

		handler->GetOpCode = OpCodeHandler_GetOpCode;

		switch (ekind)
		{
		case EncodingKind_Legacy:
			
			handler->Operands_Length = 75;

			if (code == INVALID)
			{
				OpCodeHandler_init(&handler, EFLAGS2_None, EFLAGS3_Bit16or32 | EFLAGS3_Bit64, false, NULL, 0, OpCodeHandler_GetOpCode, InvalidHandler_Encode);

				handler->handler_conf = InvalidHandler;
				handler->Encode = InvalidHandler_Encode;
				//handler = invalidHandler;
			}
			else if (code <= DeclareQword)
			{
				OpCodeHandler_init(&handler, EFLAGS2_None, EFLAGS3_Bit16or32 | EFLAGS3_Bit64, true, NULL, 0, OpCodeHandler_GetOpCode, DeclareDataHandler_Encode);

				handler->handler_conf = DeclareDataHandler;
				handler->Encode = DeclareDataHandler_Encode;
				//handler = new DeclareDataHandler(code);
			}
			else if (code == Zero_bytes)
			{
				OpCodeHandler_init(&handler, EFLAGS2_None, EFLAGS3_Bit16or32 | EFLAGS3_Bit64, true, NULL, 0, OpCodeHandler_GetOpCode, ZeroBytesHandler_Encode);

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

				OpCodeHandler_init(&handler, encFlags2, encFlags3Data, false, operands, operands_length, OpCodeHandler_GetOpCode, LegacyHandler_Encode);

				handler->handler_conf = LegacyHandler;
				//handler = new LegacyHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);
			}
			break;
		case EncodingKind_VEX:
			handler->Operands_Length = 38;

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