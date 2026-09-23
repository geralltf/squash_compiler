#include "android_base64.h"
#include <stdint.h>

static const char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void android_base64_encode(const unsigned char *data, size_t len, char *out) {
    size_t i = 0, o = 0;
    while (i + 3 <= len) {
        uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i+1] << 8) | data[i+2];
        out[o++] = TABLE[(n >> 18) & 0x3F];
        out[o++] = TABLE[(n >> 12) & 0x3F];
        out[o++] = TABLE[(n >> 6) & 0x3F];
        out[o++] = TABLE[n & 0x3F];
        i += 3;
    }
    if (len - i == 1) {
        uint32_t n = (uint32_t)data[i] << 16;
        out[o++] = TABLE[(n >> 18) & 0x3F];
        out[o++] = TABLE[(n >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (len - i == 2) {
        uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i+1] << 8);
        out[o++] = TABLE[(n >> 18) & 0x3F];
        out[o++] = TABLE[(n >> 12) & 0x3F];
        out[o++] = TABLE[(n >> 6) & 0x3F];
        out[o++] = '=';
    }
    out[o] = '\0';
}
