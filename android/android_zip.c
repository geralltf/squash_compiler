/* Minimal PKZIP writer (STORE method only) — see android_zip.h for scope.
 * Format reference: PKWARE APPNOTE.TXT sections 4.3.7 (local file header),
 * 4.3.12 (central directory file header), 4.3.16 (end of central dir). */
#include "android_zip.h"
#include "android_crc32.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char *data;
    size_t len, cap;
} bytebuf;

static void bb_init(bytebuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }

static void bb_reserve(bytebuf *b, size_t extra) {
    if (b->len + extra <= b->cap) return;
    {
        size_t newcap = b->cap ? b->cap * 2 : 4096;
        while (newcap < b->len + extra) newcap *= 2;
        b->data = (unsigned char *)realloc(b->data, newcap);
        b->cap = newcap;
    }
}

static void bb_append(bytebuf *b, const void *p, size_t n) {
    bb_reserve(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void bb_u16(bytebuf *b, uint16_t v) {
    unsigned char le[2];
    le[0] = (unsigned char)(v & 0xFF);
    le[1] = (unsigned char)((v >> 8) & 0xFF);
    bb_append(b, le, 2);
}

static void bb_u32(bytebuf *b, uint32_t v) {
    unsigned char le[4];
    le[0] = (unsigned char)(v & 0xFF);
    le[1] = (unsigned char)((v >> 8) & 0xFF);
    le[2] = (unsigned char)((v >> 16) & 0xFF);
    le[3] = (unsigned char)((v >> 24) & 0xFF);
    bb_append(b, le, 4);
}

typedef struct {
    char *name;
    unsigned char *data;
    size_t len;
    uint32_t crc;
    uint32_t local_offset;
} zip_entry;

struct android_zip_writer {
    zip_entry *entries;
    size_t n_entries, cap_entries;
};

android_zip_writer *android_zip_new(void) {
    android_zip_writer *zw = (android_zip_writer *)calloc(1, sizeof(*zw));
    return zw;
}

void android_zip_free(android_zip_writer *zw) {
    size_t i;
    if (!zw) return;
    for (i = 0; i < zw->n_entries; i++) {
        free(zw->entries[i].name);
        free(zw->entries[i].data);
    }
    free(zw->entries);
    free(zw);
}

int android_zip_add_file(android_zip_writer *zw, const char *name,
                          const void *data, size_t len) {
    zip_entry *e;
    if (zw->n_entries == zw->cap_entries) {
        size_t newcap = zw->cap_entries ? zw->cap_entries * 2 : 8;
        zw->entries = (zip_entry *)realloc(zw->entries, newcap * sizeof(zip_entry));
        zw->cap_entries = newcap;
    }
    e = &zw->entries[zw->n_entries++];
    e->name = strdup(name);
    e->data = (unsigned char *)malloc(len ? len : 1);
    if (len) memcpy(e->data, data, len);
    e->len = len;
    e->crc = android_crc32(0, data, len);
    e->local_offset = 0;
    return 0;
}

/* Fixed DOS date/time: 1980-01-01 00:00:00. APKs don't need meaningful
 * timestamps and a fixed value keeps builds byte-reproducible. */
#define DOS_TIME 0
#define DOS_DATE 0x21

int android_zip_finish(android_zip_writer *zw, unsigned char **out_data, size_t *out_len) {
    bytebuf body, central;
    size_t i;
    uint32_t central_offset, central_size;

    bb_init(&body);
    bb_init(&central);

    for (i = 0; i < zw->n_entries; i++) {
        zip_entry *e = &zw->entries[i];
        size_t namelen = strlen(e->name);

        e->local_offset = (uint32_t)body.len;

        bb_u32(&body, 0x04034b50u);
        bb_u16(&body, 20);             /* version needed */
        bb_u16(&body, 0);              /* flags */
        bb_u16(&body, 0);              /* method: store */
        bb_u16(&body, DOS_TIME);
        bb_u16(&body, DOS_DATE);
        bb_u32(&body, e->crc);
        bb_u32(&body, (uint32_t)e->len);
        bb_u32(&body, (uint32_t)e->len);
        bb_u16(&body, (uint16_t)namelen);
        bb_u16(&body, 0);              /* extra len */
        bb_append(&body, e->name, namelen);
        bb_append(&body, e->data, e->len);
    }

    central_offset = (uint32_t)body.len;

    for (i = 0; i < zw->n_entries; i++) {
        zip_entry *e = &zw->entries[i];
        size_t namelen = strlen(e->name);

        bb_u32(&central, 0x02014b50u);
        bb_u16(&central, 20);          /* version made by */
        bb_u16(&central, 20);          /* version needed */
        bb_u16(&central, 0);           /* flags */
        bb_u16(&central, 0);           /* method: store */
        bb_u16(&central, DOS_TIME);
        bb_u16(&central, DOS_DATE);
        bb_u32(&central, e->crc);
        bb_u32(&central, (uint32_t)e->len);
        bb_u32(&central, (uint32_t)e->len);
        bb_u16(&central, (uint16_t)namelen);
        bb_u16(&central, 0);           /* extra len */
        bb_u16(&central, 0);           /* comment len */
        bb_u16(&central, 0);           /* disk number start */
        bb_u16(&central, 0);           /* internal attrs */
        bb_u32(&central, (uint32_t)(0100644u << 16)); /* external attrs: -rw-r--r-- */
        bb_u32(&central, e->local_offset);
        bb_append(&central, e->name, namelen);
    }

    central_size = (uint32_t)central.len;

    bb_append(&body, central.data, central.len);
    free(central.data);

    /* EOCD */
    bb_u32(&body, 0x06054b50u);
    bb_u16(&body, 0);                  /* disk number */
    bb_u16(&body, 0);                  /* disk with central dir */
    bb_u16(&body, (uint16_t)zw->n_entries);
    bb_u16(&body, (uint16_t)zw->n_entries);
    bb_u32(&body, central_size);
    bb_u32(&body, central_offset);
    bb_u16(&body, 0);                  /* comment len */

    *out_data = body.data;
    *out_len = body.len;
    return 0;
}
