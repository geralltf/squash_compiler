#include "Assembler.h"
#include "SquashCompiler.h"
#include "FileIO.h"

#include <stdio.h>

int main(void)
{
    void* buffer;
    size_t length;
    squash_compiler_t* squash_compiler;

    printf("Wello, Horld!\n");

    if (FileReadString("scripts\\EntryPoint.c", &buffer, &length) && buffer)
    {
        squash_compiler = squash_compiler_new();
        squash_compiler_init(squash_compiler, (char*)buffer, length);

        CompileExpression(squash_compiler);
    }

    return EXIT_SUCCESS;
}