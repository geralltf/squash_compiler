#include "FileIO.h"
#include "Logger.h"

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

        return true;
    }
    else
    {
        LogCritical("FileReadString(): Error opening the file for reading! File was: '%s'", filename);
        return false;
    }
}

bool FileWriteString(char* filename, char* buffer)
{
    FILE* f = fopen(filename, "w");

    if (f == NULL) 
    {
        LogCritical("FileWriteString(): Error opening the file for writing! File was: '%s'", filename);
        return false;
    }

    fprintf(f, "%s", buffer);
    fclose(f);

    return true;
}