#ifndef ASSEMBLY_H
#define ASSEMBLY_H

#include "List.h"
#include "PrefixFlags.h"
#include "Encoder.h"

/// <summary>
/// High-Level Assembler.
/// </summary>
typedef struct Assembler
{
	//    //AbstractSyntaxTree* AST;
	list_t* stream_bytes;
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

	bool Is_macOS;
	bool Is_Linux;
	bool Is_Windows;
} assembler_t;

#endif