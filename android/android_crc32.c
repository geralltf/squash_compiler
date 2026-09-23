/* Standard CRC-32 (ISO-HDLC / zip / PNG polynomial 0xEDB88320), bit-reflected
 * table-driven implementation. This is the exact CRC used by the ZIP local
 * file header / central directory record, which is what makes an APK's
 * container a valid ZIP in the first place. */
#include "android_crc32.h"

static uint32_t g_table[256];
static int g_table_init = 0;

static void build_table(void) {
    uint32_t c;
    int n, k;
    for (n = 0; n < 256; n++) {
        c = (uint32_t)n;
        for (k = 0; k < 8; k++) {
            if (c & 1) c = 0xEDB88320u ^ (c >> 1);
            else       c = c >> 1;
        }
        g_table[n] = c;
    }
    g_table_init = 1;
}

uint32_t android_crc32(uint32_t crc, const void *buf, size_t len) {
    const unsigned char *p = (const unsigned char *)buf;
    size_t i;
    if (!g_table_init) build_table();
    crc = crc ^ 0xFFFFFFFFu;
    for (i = 0; i < len; i++) {
        crc = g_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}
