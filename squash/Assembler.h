#include "AST.h"
#include "sb.h"

#include <stdio.h>

//typedef struct Assembly_s 
//{
//	char* operandLeft;
//	char* operandRight;
//	enum OperatorType operatorType;
//	AST* ast;
//	struct Assembly_s* leftChild;
//	struct Assembly_s* rightChild;
//} Assembly;

//AST* Expr(char* inProgramExpression);
//bool Compile(AST* programAST, ASMTree** programASM);

typedef struct Assembler
{
    /// <summary>
    /// The expression tree output from a specified ExpressionCompiler.
    /// </summary>
    //AbstractSyntaxTree* AST;

    bool Is_macOS;
    bool Is_Linux;
    bool Is_Windows;
} assembler_t;

/// <summary>
/// Generates assembly language code given specified AST.
/// </summary>
/// <exception cref="Exception">
/// Underlying Assemble() method can throw exceptions as well as this method.
/// </exception>
void GenerateCode(assembler_t* assembler, astnode_t* astNode);

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
char* Assemble(assembler_t* assembler, astnode_t* node);