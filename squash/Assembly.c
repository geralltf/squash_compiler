#include "Assembly.h"

bool Preprocessor(char* inSourceFile, char* includesDir, char* srcDir, char* objDir)
{
    return true;
}
AST* ExprLexeme(char* inProgramExpression)
{
    return Expr(inProgramExpression);
}
bool Compile(AST* programAST, ASMTree** programASM)
{
    ASMTree* result = (ASMTree*)malloc(sizeof(ASMTree*));

    (*programASM) = result;

    return true;
}
bool Link(ASMTree* program, char* outDir, char* objFiles, Assembly** outAssembly, char* outProgramBuildFilenameASM)
{
    return true;
}
bool CodeGeneratorAssembler(Assembly* assembly, char* outProgramBuildFilenameASM, char** objDir)
{
    return true;
}
bool ILCodeGenerator(Assembly** assembly, char* outProgramBuildFilenameIL)
{
    return true;
}
bool AssembleBinaryExecutable(Assembly* inAssembly, char* objDir, char* inProgramBuildFilenameASM, char* outProgramBuildFilenameBIN)
{
    return true;
}
bool AssembleBinarySharedLibrary(Assembly* inAssembly, char* objDir, char* inProgramBuildFilenameASM, char* outProgramBuildFilenameBIN)
{
    return true;
}
