#include "Assembly.h"

#include <stdio.h>

int main(void)
{
    printf("Wello, Horld!");

    //List<AST> computeResult = AST.Expr("int a = 2 / 8 * (9 + 2) + 3+1;");
    // 1 + (2*3)
    // (2*3) + 1

    if(Preprocessor("test.c", "includes", "src", "obj"))
    {
        AST* computeResult = Expr("int a = 2 / 8 * (90 + 20) + 3+1 - 4*8 +7*3 + 12;");
        PrintAST(computeResult);

        /*
        List<ASMTree> outASMComputeResult;

        if(Assembly.Compile(computeResult, out outASMComputeResult))
        {
            Assembly* asm;

            if(Assembly.Link(outASMComputeResult, "bin", new string[] { "test.obj"}, out asm, "test.asm"))
            {
                if(Assembly.CodeGeneratorAssembler(ref asm, "test.asm64", "obj"))
                {

                }
                else
                {

                }
            }
            else
            {

            }
        } 
        */
    }
    else
    {

    }

    return EXIT_SUCCESS;
}