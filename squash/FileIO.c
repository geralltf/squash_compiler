#include "FileIO.h"

bool FileReadString(char* filename, void** buffer, size_t* file_length)
{
    char* buffer = 0;
    size_t length;
    FILE* f = fopen(filename, "rb");

    if (f)
    {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        *file_length = length;
        fseek(f, 0, SEEK_SET);
        *buffer = malloc(length);
        if (*buffer)
        {
            fread(*buffer, 1, length, f);
        }
        fclose(f);
    }
}