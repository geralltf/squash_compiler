#ifndef ASSEMBLY_H
#define ASSEMBLY_H

#include <stdlib.h>
#include <stdbool.h>
#include "AST.h"
#include "ASM.h"

typedef struct Assembly_s 
{
	char* operandLeft;
	char* operandRight;
	enum OperatorType operatorType;
	AST* ast;
	struct Assembly_s* leftChild;
	struct Assembly_s* rightChild;
} Assembly;

bool Preprocessor(char* inSourceFile, char* includesDir, char* srcDir, char* objDir);
AST* Expr(char* inProgramExpression);
bool Compile(AST* programAST, ASMTree** programASM);
bool Link(ASMTree* program, char* outDir, char* objFiles, Assembly** outAssembly, char* outProgramBuildFilenameASM);
bool CodeGeneratorAssembler(Assembly* assembly, char* outProgramBuildFilenameASM, char** objDir);
bool ILCodeGenerator(Assembly** assembly, char* outProgramBuildFilenameIL);
bool AssembleBinaryExecutable(Assembly* inAssembly, char* objDir, char* inProgramBuildFilenameASM, char* outProgramBuildFilenameBIN);
bool AssembleBinarySharedLibrary(Assembly* inAssembly, char* objDir, char* inProgramBuildFilenameASM, char* outProgramBuildFilenameBIN);


#endif