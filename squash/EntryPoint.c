#include "Assembler.h"
#include "SquashCompiler.h"
#include "FileIO.h"
#include "Logger.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[])
{
    void* buffer = NULL;
    size_t length = 0;
    squash_compiler_t* squash_compiler = NULL;
    char* arg = NULL;
    char* prev_arg = NULL;
    int index = 0;
    int next = 0;
    int previous = 0;
    bool enable_tracing = false;
    bool inputted_csource = false;
    bool optimise = false;
    bool output_asm = false;
    bool output_binary = false;
    char* input_file_name = NULL;
    char* output_file_name = NULL;
    char* output_bin_file_name = NULL;
    
    printf("*-*-*-*-*-*-* SquashC Compiler *-*-*-*-*-*-*\n");

    for (index = 0; index < argc; index++)
    {
        next = index + 1;
        previous = index - 1;
        arg = argv[index];

        if (strcmp(arg, "-v") == 0)
        {
            enable_tracing = true;
        }

        if (enable_tracing)
        {
            printf("  %d: %s\n", index, arg);
        }
        
        if (strcmp(arg, "-v") == 0)
        {
            enable_tracing = true;
        }
        else if (strcmp(arg, "-S") == 0 || strcmp(arg, "-s") == 0)
        {
            if (next > 0 && next < argc)
            {
                output_asm = true;
                output_file_name = argv[next];
            }
        }
        else if (strcmp(arg, "-o") == 0) // Output executable binary file argument. Next argument is the output file name of the binary.
        {
            if (next >= 0 && next < argc)
            {
                output_binary = true;
                output_bin_file_name = argv[next];
            }
        }
        else if (strcmp(arg, "-O") == 0) // Same as -O1
        {
            optimise = true;
            LogCritical("%s", "Optimisation level '-O' is not yet implemented.");
        }
        else if (strcmp(arg, "-O0") == 0) // do no optimize, the default if no optimization level is specified
        {
            optimise = false;
        }
        else if (strcmp(arg, "-O1") == 0) // optimize minimally, favoring compilation time
        {
            optimise = true;
            LogCritical("%s", "Optimisation level '-O1' is not yet implemented.");
        }
        else if (strcmp(arg, "-O2") == 0) // optimize more, without speed/size tradeoff
        {
            optimise = true;
            LogCritical("%s", "Optimisation level '-O2' is not yet implemented.");
        }
        else if (strcmp(arg, "-O3") == 0) // optimize even more, favoring speed
        {
            optimise = true;
            LogCritical("%s", "Optimisation level '-O3' is not yet implemented.");
        }
        else if (strcmp(arg, "-Ofast") == 0) // optimize very aggressively to the point of breaking standard compliance, favoring speed. May change program behavior
        {
            optimise = true;
            LogCritical("%s", "Optimisation level '-Ofast' is not yet implemented.");
        }
        else if (strcmp(arg, "-Og") == 0) 
        {
            // Optimize debugging experience. -Og enables optimizations that do not interfere with debugging. 
            // It should be the optimization level of choice for the standard edit-compile-debug cycle, 
            // offering a reasonable level of optimization while maintaining fast compilation and a good debugging experience.

            optimise = true;
            LogCritical("%s", "Optimisation level '-Og' is not yet implemented.");
        }
        else if (strcmp(arg, "-Os") == 0)
        {
            // Optimize for size. -Os enables all -O2 optimizations that do not typically increase code size. 
            // It also performs further optimizations designed to reduce code size. 
            // -Os disables the following optimization flags: 
            //      -falign-functions -falign-jumps -falign-loops -falign-labels -freorder-blocks 
            //      -freorder-blocks-and-partition -fprefetch-loop-arrays -ftree-vect-loop-version

            optimise = true;
            LogCritical("%s", "Optimisation level '-Os' is not yet implemented.");
        }
        else
        {
            // The current argument must be the file name of the source .c file to be compiled or the output .s file of the assembly output.

            if (previous >= 0 && previous < argc)
            {
                prev_arg = argv[previous];

                if (strcmp(arg, "-v") == 0)
                {
                    // Ignore because this setting has already been applied.
                }
                else if (strcmp(prev_arg, "-S") == 0 || strcmp(prev_arg, "-s") == 0)
                {
                    output_asm = true;
                    output_file_name = arg;
                }
                else if (strcmp(prev_arg, "-o") == 0)
                {
                    output_binary = true;
                    output_bin_file_name = arg;
                }
                else if (arg[0] == '-' && arg[1] == 'O')
                {
                    // Ignore because of optimiser level selector.
                }
                else if(index > 0)
                {
                    inputted_csource = true;
                    input_file_name = arg;
                }
            }
        }
    }

    if (inputted_csource)
    {
        LogInformation("\t.c source specified at: '%s'", input_file_name);
    }

    if (output_asm)
    {
        LogInformation("\t.s assembly output specified at: '%s'", output_file_name);
    }

    if (output_binary)
    {
        LogInformation("\t binary executable output specified at: '%s'", output_bin_file_name);
    }

    if (inputted_csource && output_asm)
    {
        if (FileReadString(input_file_name, &buffer, &length) && buffer)
        {
            squash_compiler = squash_compiler_new();
            squash_compiler_init(squash_compiler, (char*)buffer, length);

            CompileExpression(squash_compiler, output_file_name, enable_tracing);
        }
    }

    if (output_asm && output_binary)
    {
        // Compile .s file into specified executable or object file.

        LogCritical("Compile .s file into specified executable or object file not implemented.");
    }

    return EXIT_SUCCESS;
}