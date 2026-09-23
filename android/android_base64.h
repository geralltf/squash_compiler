#ifndef ANDROID_BASE64_H
#define ANDROID_BASE64_H
#include <stddef.h>

/* Standard (RFC 4648) base64 encoding with padding, used for JAR manifest
 * digest values (MANIFEST.MF / *.SF). Writes a NUL-terminated string into
 * `out` (caller-allocated, must be at least 4*ceil(len/3)+1 bytes). */
void android_base64_encode(const unsigned char *data, size_t len, char *out);

#endif
