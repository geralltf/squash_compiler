#ifndef BUFFER_H
#define BUFFER_H

#include "debugent.h"

bool readByte(bounded_buffer* b, uint32_t offset, uint8_t* out);
bool readWord(bounded_buffer* b, uint32_t offset, uint16_t* out);
bool readDword(bounded_buffer* b, uint32_t offset, uint32_t* out);
bool readQword(bounded_buffer* b, uint32_t offset, uint64_t* out);
bool readChar16(bounded_buffer* b, uint32_t offset, char16_t* out);

bounded_buffer* readFileToFileBuffer(const char* filePath);
bounded_buffer* makeBufferFromPointer(uint8_t* data, uint32_t sz);
bounded_buffer* splitBuffer(bounded_buffer* b, uint32_t from, uint32_t to);
void deleteBuffer(bounded_buffer* b);
uint64_t bufLen(bounded_buffer* b);

#endif