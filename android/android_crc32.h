#ifndef ANDROID_CRC32_H
#define ANDROID_CRC32_H
#include <stdint.h>
#include <stddef.h>

uint32_t android_crc32(uint32_t crc, const void *buf, size_t len);

#endif
