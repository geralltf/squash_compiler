#include "android_adler32.h"

#define ADLER_MOD 65521u

uint32_t android_adler32(const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    uint32_t a = 1, b = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        a = (a + p[i]) % ADLER_MOD;
        b = (b + a) % ADLER_MOD;
    }
    return (b << 16) | a;
}
