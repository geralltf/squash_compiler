#ifndef ASM_ENCODER
#define ASM_ENCODER

// Ported to C language from the C# version of Iced found here: https://github.com/icedland/iced/blob/master/src/csharp/Intel/README.md

#include "List.h"
#include "Logger.h"
#include "Instruction.h"
#include "EncoderInstructionSet.h"

enum PrefixFlags
{
	PF_None = 0,
	PF_Lock = 0x01,
	PF_Repe = 0x02,
	PF_Repne = 0x04,
	PF_Notrack = 0x08,
	PF_PreferVex = 0x10,
	PF_PreferEvex = 0x20,
};

enum RoundingControl 
{
	/// <summary>No rounding mode</summary>
	RC_None = 0,
	/// <summary>Round to nearest (even)</summary>
	RC_RoundToNearest = 1,
	/// <summary>Round down (toward -inf)</summary>
	RC_RoundDown = 2,
	/// <summary>Round up (toward +inf)</summary>
	RC_RoundUp = 3,
	/// <summary>Round toward zero (truncate)</summary>
	RC_RoundTowardZero = 4,
};

enum AssemblerOperandFlags 
{
	/// <summary>
	/// No flags.
	/// </summary>
	AF_None = 0,
	/// <summary>
	/// Broadcast.
	/// </summary>
	AF_Broadcast = 1,
	/// <summary>
	/// Zeroing mask.
	/// </summary>
	AF_Zeroing = 1 << 1,
	/// <summary>
	/// Suppress all exceptions (.sae).
	/// </summary>
	AF_SuppressAllExceptions = 1 << 2,
	/// <summary>
	/// Round to nearest (.rn_sae).
	/// </summary>
	AF_RoundToNearest = RC_RoundToNearest << 3,
	/// <summary>
	/// Round to down (.rd_sae).
	/// </summary>
	AF_RoundDown = RC_RoundDown << 3,
	/// <summary>
	/// Round to up (.ru_sae).
	/// </summary>
	AF_RoundUp = RC_RoundUp << 3,
	/// <summary>
	/// Round towards zero (.rz_sae).
	/// </summary>
	AF_RoundTowardZero = RC_RoundTowardZero << 3,
	/// <summary>
	/// RoundingControl mask.
	/// </summary>
	AF_RoundingControlMask = 0x7 << 3,
	/// <summary>
	/// Mask register K1.
	/// </summary>
	AF_K1 = 1 << 6,
	/// <summary>
	/// Mask register K2.
	/// </summary>
	AF_K2 = 2 << 6,
	/// <summary>
	/// Mask register K3.
	/// </summary>
	AF_K3 = 3 << 6,
	/// <summary>
	/// Mask register K4.
	/// </summary>
	AF_K4 = 4 << 6,
	/// <summary>
	/// Mask register K5.
	/// </summary>
	AF_K5 = 5 << 6,
	/// <summary>
	/// Mask register K6.
	/// </summary>
	AF_K6 = 6 << 6,
	/// <summary>
	/// Mask register K7.
	/// </summary>
	AF_K7 = 7 << 6,
	/// <summary>
	/// Mask for K registers.
	/// </summary>
	AF_RegisterMask = 0x7 << 6,
};

enum RepPrefixKind 
{
	/// <summary>No <c>REP</c>/<c>REPE</c>/<c>REPNE</c> prefix</summary>
	RPK_None = 0,
	/// <summary><c>REP</c>/<c>REPE</c> prefix</summary>
	RPK_Repe = 1,
	/// <summary><c>REPNE</c> prefix</summary>
	RPK_Repne = 2,
};

/// <summary>
/// A label that can be created by <see cref="Assembler.CreateLabel"/>.
/// </summary>
struct Label
{
	char* name;
	unsigned long id;
	int instruction_index;
};

/// <summary>
/// High-Level Assembler.
/// </summary>
struct Assembler
{
	list_t* instructions;
	unsigned long currentLabelId;
	struct Label* currentLabel;
	struct Label* currentAnonLabel;
	struct Label* nextAnonLabel;
	bool definedAnonLabel;
	enum PrefixFlags prefixFlags;

	/// <summary>
	/// Gets the bitness defined for this assembler.
	/// </summary>
	int Bitness;

	/// <summary>
	/// <c>true</c> to prefer VEX encoding over EVEX. This is the default. See also <see cref="vex"/> and <see cref="evex"/>.
	/// </summary>
	bool PreferVex;

	/// <summary>
	/// <c>true</c> to prefer short branch encoding. This is the default.
	/// </summary>
	bool PreferShortBranch;
};

/// <summary>
/// Creates a new instance of this assembler
/// </summary>
/// <param name="bitness">The assembler instruction set bitness, either 16, 32 or 64 bit.</param>
void assembler(struct Assembler* assembler, int bitness);

bool InstructionPreferVex(struct Assembler* assembler);

/// <summary>
/// Reset the current set of instructions and labels added to this instance.
/// </summary>
void Reset(struct Assembler* assembler);

int instructions_count(struct Assembler* assembler);

/// <summary>
/// Creates a label.
/// </summary>
/// <param name="name">Optional name of the label.</param>
/// <returns></returns>
struct Label* create_label(struct Assembler* assembler, char* name);

/// <summary>
/// Use the specified label.
/// </summary>
/// <param name="label">Label to use</param>
/// <exception cref="ArgumentException"></exception>
void label(struct Assembler* assembler, struct Label* label);

/// <summary>
/// Creates an anonymous label that can be referenced by using <see cref="B"/> (backward anonymous label)
/// and <see cref="F"/> (forward anonymous label).
/// </summary>
void anonymous_label(struct Assembler* assembler);

/// <summary>
/// References the previous anonymous label created by <see cref="AnonymousLabel"/>
/// </summary>
struct Label* B(struct Assembler* assembler); // @B

/// <summary>
/// References the next anonymous label created by a future call to <see cref="AnonymousLabel"/>
/// </summary>
struct Label* F(struct Assembler* assembler); // @F

/// <summary>
/// Add an instruction directly to the flow of instructions.
/// </summary>
/// <param name="instruction"></param>
void AddInstruction(struct Assembler* assembler, struct Instruction* instruction);

/// <summary>
/// Add an instruction directly to the flow of instructions.
/// </summary>
/// <param name="instruction"></param>
/// <param name="flags">Operand flags passed.</param>
void AddInstruction(struct Assembler* assembler, struct Instruction* instruction, enum AssemblerOperandFlags flags);

/// <summary>
/// Add lock prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* lock(struct Assembler* assembler);

/// <summary>
/// Add xacquire prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* xacquire(struct Assembler* assembler);

/// <summary>
/// Add xrelease prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* xrelease(struct Assembler* assembler);

/// <summary>
/// Add rep prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* rep(struct Assembler* assembler);

/// <summary>
/// Add repe/repz prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* repe(struct Assembler* assembler);

/// <summary>
/// Add repe/repz prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* repz(struct Assembler* assembler);

/// <summary>
/// Add repne/repnz prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* repne(struct Assembler* assembler);

/// <summary>
/// Add repne/repnz prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* repnz(struct Assembler* assembler);

/// <summary>
/// Add bnd prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* bnd(struct Assembler* assembler);

/// <summary>
/// Add notrack prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* notrack(struct Assembler* assembler);

/// <summary>
/// Prefer VEX encoding if the next instruction can be VEX and EVEX encoded
/// </summary>
/// <returns></returns>
struct Assembler* vex(struct Assembler* assembler);

/// <summary>
/// Prefer EVEX encoding if the next instruction can be VEX and EVEX encoded
/// </summary>
/// <returns></returns>
struct Assembler* evex(struct Assembler* assembler);

/// <summary>
/// Adds data
/// </summary>
/// <param name="array">Data</param>
void db(unsigned char* array);

/// <summary>
/// Adds data
/// </summary>
/// <param name="array">Data</param>
/// <param name="index">Start index</param>
/// <param name="length">Length in bytes</param>
void db(unsigned char* array, int index, int length);

/// <summary>call selector:offset instruction.</summary>
void call(unsigned short selector, unsigned int offset);

/// <summary>jmp selector:offset instruction.</summary>
void jmp(unsigned short selector, unsigned int offset);

/// <summary>xlatb instruction.</summary>
void xlatb();

/// <summary>
/// Generates multibyte NOP instructions
/// </summary>
/// <param name="sizeInBytes">Size in bytes of all nops</param>
void nop(int sizeInBytes);

/// <summary>
/// Encodes instructions. Any number of branches can be part of this block.
/// You can use this function to move instructions from one location to another location.
/// If the target of a branch is too far away, it'll be rewritten to a longer branch.
/// You can disable this by passing in <see cref="BlockEncoderOptions.DontFixBranches"/>.
/// If the block has any <c>RIP</c>-relative memory operands, make sure the data isn't too
/// far away from the new location of the encoded instructions. Every OS should have
/// some API to allocate memory close (+/-2GB) to the original code location.
/// </summary>
/// <param name="bitness">16, 32 or 64</param>
/// <param name="blocks">All instructions</param>
/// <param name="errorMessage">Updated with an error message if the method failed</param>
/// <param name="result">Result if this method returns <see langword="true"/></param>
/// <param name="options">Encoder options</param>
/// <returns></returns>
bool TryEncode(int bitness, InstructionBlock[] blocks, [NotNullWhen(false)] out string ? errorMessage, [NotNullWhen(true)] out BlockEncoderResult[] ? result, BlockEncoderOptions options = BlockEncoderOptions.None);

bool Encode([NotNullWhen(false)] out string ? errorMessage, [NotNullWhen(true)] out BlockEncoderResult[] ? result);

/// <summary>
/// Assembles the instructions of this assembler with the specified options.
/// </summary>
/// <param name="writer">The code writer.</param>
/// <param name="rip">Base address.</param>
/// <param name="options">Encoding options.</param>
/// <returns></returns>
/// <exception cref="InvalidOperationException"></exception>
struct AssemblerResult* Assemble(struct CodeWriter* writer, unsigned long rip, enum BlockEncoderOptions options);

/// <summary>
/// Tries to assemble the instructions of this assembler with the specified options.
/// </summary>
/// <param name="writer">The code writer.</param>
/// <param name="rip">Base address.</param>
/// <param name="errorMessage">Error messages.</param>
/// <param name="assemblerResult">The assembler result if successful.</param>
/// <param name="options">Encoding options.</param>
/// <returns><c>true</c> if the encoding was successful; <c>false</c> otherwise.</returns>
bool TryAssemble(struct CodeWriter* writer, unsigned long rip, char** errorMessage, struct AssemblerResult** assemblerResult, enum BlockEncoderOptions options); // = BlockEncoderOptions.None

/// <summary>
/// Internal method used to throw an InvalidOperationException if it was not possible to encode an OpCode.
/// </summary>
/// <param name="mnemonic">The mnemonic of the instruction</param>
/// <param name="argNames">The argument values.</param>
/// <returns></returns>
//InvalidOperationException NoOpCodeFoundFor(Mnemonic mnemonic, params object[] argNames);

/// <summary>
/// Writes the next byte
/// </summary>
/// <param name="value">Value</param>
void WriteByte(unsigned char* buffer, unsigned char value);

void test_assembler();

#endif