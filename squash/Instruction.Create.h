#ifndef INSTRUCTION_CREATE
#define INSTRUCTION_CREATE

#include "OpCodeInfo.h"
#include "Instruction.h"
#include "OpCodeInfoData.h"

struct OpCodeHandler
{
	void* derived;
	unsigned int OpCode;
	bool Is2ByteOpCode;
	int GroupIndex;
	int RmGroupIndex;
	bool IsSpecialInstr;
	enum EncFlags3 EncFlags3;
	enum CodeSize OpSize;
	enum CodeSize AddrSize;
	//TryConvertToDisp8N ? TryConvertToDisp8N;
	struct Op* Operands;

	unsigned int (*GetOpCode)(struct OpCodeHandler* self, enum EncFlags2 encFlags2);
	void (*Encode)(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction);

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

void OpCodeHandler_init(struct OpCodeHandler** o, 
	enum EncFlags2 encFlags2, 
	enum EncFlags3 encFlags3, 
	bool isSpecialInstr, 
	/*TryConvertToDisp8N ? tryConvertToDisp8N, */ 
	struct Op* operands,
	unsigned int (*GetOpCode)(struct OpCodeHandler* self, enum EncFlags2 encFlags2),
	void (*Encode)(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction))
{
	(*o)->GetOpCode = GetOpCode;
	(*o)->Encode = Encode;
}

unsigned int InvalidHandler_GetOpCode(struct OpCodeHandler* self, enum EncFlags2 encFlags2)
{
	return (unsigned short)((unsigned int)encFlags2 >> (int)EFLAGS2_OpCodeShift);
}

void InvalidHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction)
{
	//const string ERROR_MESSAGE = "Can't encode an invalid instruction";
}

void InvalidHandler_init(struct InvalidHandler** o, 
	enum EncFlags2 encFlags2,
	enum EncFlags3 encFlags3,
	bool isSpecialInstr,
	/*TryConvertToDisp8N ? tryConvertToDisp8N, */
	struct Op* operands)
{
	OpCodeHandler_init(&((*o)->base), encFlags2, encFlags3, isSpecialInstr, operands, &InvalidHandler_GetOpCode, &InvalidHandler_Encode);
}


unsigned int DeclareDataHandler_GetOpCode(struct OpCodeHandler* self, enum EncFlags2 encFlags2)
{
	return (unsigned short)((unsigned int)encFlags2 >> (int)EFLAGS2_OpCodeShift);
}

void DeclareDataHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction)
{
	struct DeclareDataHandler* derived = (struct DeclareDataHandler*)self->derived;

	int declDataCount = GetDeclareDataCount(instruction);

	if (declDataCount < 1 || declDataCount > derived->maxLength) {
		//encoder.ErrorMessage = $"Invalid db/dw/dd/dq data count. Count = {declDataCount}, max count = {maxLength}";
		return;
	}
	int length = declDataCount * derived->elemLength;
	for (int i = 0; i < length; i++)
	{
		unsigned char b = GetDeclareByteValue(instruction, i);

		encoder.WriteByteInternal(b);
	}
}

void DeclareDataHandler_init(struct DeclareDataHandler** o, 
	enum Code code,
	enum EncFlags2 encFlags2,
	enum EncFlags3 encFlags3,
	bool isSpecialInstr,
	/*TryConvertToDisp8N ? tryConvertToDisp8N, */
	struct Op* operands)
	//: base(EncFlags2.None, EncFlags3.Bit16or32 | EncFlags3.Bit64, true, null, Array2.Empty<Op>()) 
{
	int elemLength;

	OpCodeHandler_init(&((*o)->base), encFlags2, encFlags3, isSpecialInstr, operands, &DeclareDataHandler_GetOpCode, &DeclareDataHandler_Encode);
	(*o)->base->derived = (*o);

	switch (code)
	{
	case DeclareByte:
		elemLength = 1;
		break;
	case DeclareWord:
		elemLength = 2;
		break;
	case DeclareDword:
		elemLength = 4;
		break;
	case DeclareQword:
		elemLength = 8;
		break;
	default:
		// throw new InvalidOperationException();
		break;
	}
	(*o)->elemLength = elemLength;
	(*o)->maxLength = 16 / elemLength;
}

/// <summary>
/// Creates a new far branch instruction
/// </summary>
/// <param name="code">Code value</param>
/// <param name="selector">Selector/segment value</param>
/// <param name="offset">Offset</param>
struct Instruction* CreateBranch(enum Code code, unsigned short selector, unsigned int offset);

#endif