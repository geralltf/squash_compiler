#ifndef ANDROID_ADLER32_H
#define ANDROID_ADLER32_H
#include <stdint.h>
#include <stddef.h>

/* DEX file format's header `checksum` field is Adler-32 over everything
 * after it (i.e. everything except the magic+checksum themselves). Same
 * algorithm zlib/gzip use for their own Adler-32, unrelated to CRC32
 * (android_crc32.c, used for ZIP entries). */
uint32_t android_adler32(const void *data, size_t len);

#endif
