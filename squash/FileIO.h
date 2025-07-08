#ifndef FILE_H
#define FILE_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

bool FileReadString(char* filename, void** buffer, size_t* file_length);
bool FileWriteString(char* filename, char* buffer);

#endif