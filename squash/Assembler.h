#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include "AST.h"
#include "sb.h"
#include "FileIO.h"
#include "Encoder.h"

#include <stdio.h>

/// <summary>
/// High-Level Assembler.
/// </summary>
struct Assembler
{
	//    //AbstractSyntaxTree* AST;
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
};


struct Assembler* assembler_new();

/// <summary>
/// Generates assembly language code given specified AST.
/// </summary>
/// <exception cref="Exception">
/// Underlying Assemble() method can throw exceptions as well as this method.
/// </exception>
void GenerateCode(struct Assembler* assembler, astnode_t* astNode, char* output_file_name, char* output_binary_file_name, bool enable_tracing);

/// <summary>
/// Assembles the specified ASTNode into assembly language instructions.
/// This is the compiler backend.
/// </summary>
/// <param name="node">
/// The specified ASTNode to compile into assembly instructions.
/// </param>
/// <returns>
/// A string comprising a set of assembly language instructions to be further assembled to machine code.
/// </returns>
/// <exception cref="Exception">
/// Can throw exceptions related to invalid ASTNodeType types.
/// </exception>
char* Assemble(struct Assembler* assembler, astnode_t* node);

void squash_assembler(struct Assembler* assembler, char* source_asm, int source_size, char* output_binary_file_name);

#endif