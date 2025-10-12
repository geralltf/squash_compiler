#include "Instruction.Create.h"

//struct OpCodeHandler* EncoderInternal_OpCodeHandlers_Handlers[4936] = // CodeEnumCount
//{
//
//};

struct OpCodeHandler* OpCodeHandlers_init(enum Code opcode)
{
	auto encFlags1 = EncoderData_EncFlags1;
	auto encFlags2 = EncoderData_EncFlags2;
	auto encFlags3Data = EncoderData_EncFlags3;
	//auto handlers = new OpCodeHandler[IcedConstants.CodeEnumCount];
	int i = (int)opcode;
	auto invalidHandler = new InvalidHandler();
	//for (; i < 3936; i++) 
	//{
		enum EncFlags3 encFlags3 = (enum EncFlags3)encFlags3Data[i];
		struct OpCodeHandler* handler;
		enum EncodingKind ekind = (enum EncodingKind)(((unsigned int)encFlags3 >> (int)EFLAGS3_EncodingShift) & (unsigned int)EFLAGS3_EncodingMask);
		switch (ekind)
		{
		case EncodingKind_Legacy:
			enum Code code = (enum Code)i;
			if (code == INVALID)
			{
				handler = invalidHandler;
			}
			else if (code <= DeclareQword)
			{
				handler = new DeclareDataHandler(code);
			}
			else if (code == Zero_bytes)
			{
				handler = new ZeroBytesHandler(code);
			}
			else
			{
				handler = new LegacyHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);
			}
			break;

		case EncodingKind_VEX:
			handler = new VexHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);
			break;

		case EncodingKind_EVEX:
			handler = new EvexHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);
			break;

		case EncodingKind_XOP:
			handler = new XopHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);
			break;

		case EncodingKind_D3NOW:
			handler = new D3nowHandler((enum EncFlags2)encFlags2[i], encFlags3);
			break;

		case EncodingKind_MVEX:
			handler = new MvexHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);

			break;

		default:
			//throw new InvalidOperationException();
			break;
		}
		return handler;
		//handlers[i] = handler;
		//EncoderInternal_OpCodeHandlers_Handlers[i] = handler;
	//}
	//if (i != handlers.Length)
		//throw new InvalidOperationException();
	//Handlers = handlers;
}

enum OpKind GetImmediateOpKind(enum Code code, int operand) 
{
	auto handlers = EncoderInternal_OpCodeHandlers_Handlers;
	if ((unsigned int)code >= (unsigned int)4936)
	{
		//throw new ArgumentOutOfRangeException(nameof(code));
	}
	var operands = handlers[(int)code].Operands;
	if ((uint)operand >= (uint)operands.Length)
		throw new ArgumentOutOfRangeException(nameof(operand), $"{code} doesn't have at least {operand + 1} operands");
	var opKind = operands[operand].GetImmediateOpKind();
	if (opKind == OpKind.Immediate8 &&
		operand > 0 &&
		operand + 1 == operands.Length &&
		operands[operand - 1].GetImmediateOpKind() is OpKind opKindPrev &&
		(opKindPrev == OpKind.Immediate8 || opKindPrev == OpKind.Immediate16)) {
		opKind = OpKind.Immediate8_2nd;
	}
	if (opKind == (OpKind)(-1))
		throw new ArgumentException($"{code}'s op{operand} isn't an immediate operand");
	return opKind;
}

enum OpKind GetNearBranchOpKind(enum Code code, int operand) 
{
	var handlers = EncoderInternal.OpCodeHandlers.Handlers;
	if ((uint)code >= (uint)handlers.Length)
		throw new ArgumentOutOfRangeException(nameof(code));
	var operands = handlers[(int)code].Operands;
	if ((uint)operand >= (uint)operands.Length)
		throw new ArgumentOutOfRangeException(nameof(operand), $"{code} doesn't have at least {operand + 1} operands");
	var opKind = operands[operand].GetNearBranchOpKind();
	if (opKind == (OpKind)(-1))
		throw new ArgumentException($"{code}'s op{operand} isn't a near branch operand");
	return opKind;
}

enum OpKind GetFarBranchOpKind(enum Code code, int operand) 
{
	var handlers = EncoderInternal.OpCodeHandlers.Handlers;
	if ((uint)code >= (uint)handlers.Length)
		throw new ArgumentOutOfRangeException(nameof(code));
	var operands = handlers[(int)code].Operands;
	if ((uint)operand >= (uint)operands.Length)
		throw new ArgumentOutOfRangeException(nameof(operand), $"{code} doesn't have at least {operand + 1} operands");
	var opKind = operands[operand].GetFarBranchOpKind();
	if (opKind == (OpKind)(-1))
		throw new ArgumentException($"{code}'s op{operand} isn't a far branch operand");
	return opKind;
}

/// <summary>
/// Creates a new near/short branch instruction
/// </summary>
/// <param name="code">Code value</param>
/// <param name="target">Target address</param>
struct Instruction* CreateBranch(enum Code code, unsigned long target) 
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
struct Instruction* CreateBranch(enum Code code, unsigned short selector, unsigned int offset)
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