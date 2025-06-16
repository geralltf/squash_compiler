#include "FileIO.h"

bool FileReadString(char* filename, void** filebuffer, size_t* file_length)
{
    char* buffer = *filebuffer;
    size_t length;
    FILE* f = fopen(filename, "rb");

    if (f)
    {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        *file_length = length;
        fseek(f, 0, SEEK_SET);
        buffer = (char*)malloc(sizeof(char)*length+1);
        if (buffer)
        {
            fread(buffer, 1, length, f);
            buffer[length] = '\0';
        }
        fclose(f);

        *filebuffer = buffer;
    }
}