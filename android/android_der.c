#include "android_der.h"
#include <stdlib.h>
#include <string.h>

void der_buf_init(der_buf *b) { b->data = NULL; b->len = 0; b->cap = 0; }
void der_buf_free(der_buf *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

static void der_reserve(der_buf *b, size_t extra) {
    if (b->len + extra <= b->cap) return;
    { size_t nc = b->cap ? b->cap * 2 : 256;
      while (nc < b->len + extra) nc *= 2;
      b->data = (unsigned char *)realloc(b->data, nc);
      b->cap = nc; }
}

void der_buf_append(der_buf *b, const void *p, size_t n) {
    der_reserve(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void der_write_len(der_buf *out, size_t len) {
    if (len < 128) {
        unsigned char b = (unsigned char)len;
        der_buf_append(out, &b, 1);
    } else {
        unsigned char lenbytes[8];
        int n = 0;
        size_t l = len;
        while (l) { lenbytes[n++] = (unsigned char)(l & 0xFF); l >>= 8; }
        { unsigned char hdr = (unsigned char)(0x80 | n);
          der_buf_append(out, &hdr, 1); }
        { int i; for (i = n - 1; i >= 0; i--) der_buf_append(out, &lenbytes[i], 1); }
    }
}

void der_tlv(der_buf *out, unsigned char tag, const void *content, size_t content_len) {
    der_buf_append(out, &tag, 1);
    der_write_len(out, content_len);
    if (content_len) der_buf_append(out, content, content_len);
}

void der_sequence(der_buf *out, const der_buf *inner) {
    der_tlv(out, 0x30, inner->data, inner->len);
}

void der_integer_from_bignum(der_buf *out, const bignum *v) {
    int nbits = bn_bitlen(v);
    int nbytes = (nbits + 7) / 8;
    int needs_leading_zero;
    unsigned char *buf;

    if (nbytes == 0) { unsigned char zero = 0; der_tlv(out, 0x02, &zero, 1); return; }

    buf = (unsigned char *)malloc((size_t)nbytes + 1);
    bn_to_bytes_be(v, buf, nbytes);
    needs_leading_zero = (buf[0] & 0x80) != 0;
    if (needs_leading_zero) {
        unsigned char *padded = (unsigned char *)malloc((size_t)nbytes + 1);
        padded[0] = 0;
        memcpy(padded + 1, buf, (size_t)nbytes);
        der_tlv(out, 0x02, padded, (size_t)nbytes + 1);
        free(padded);
    } else {
        der_tlv(out, 0x02, buf, (size_t)nbytes);
    }
    free(buf);
}

void der_integer_u32(der_buf *out, uint32_t v) {
    bignum b;
    bn_set_u32(&b, v);
    der_integer_from_bignum(out, &b);
}

void der_null(der_buf *out) {
    der_tlv(out, 0x05, NULL, 0);
}

void der_oid(der_buf *out, const unsigned char *oid_bytes, size_t oid_len) {
    der_tlv(out, 0x06, oid_bytes, oid_len);
}

void der_bitstring(der_buf *out, const void *content, size_t content_len) {
    unsigned char *tmp = (unsigned char *)malloc(content_len + 1);
    tmp[0] = 0; /* zero unused bits */
    memcpy(tmp + 1, content, content_len);
    der_tlv(out, 0x03, tmp, content_len + 1);
    free(tmp);
}

void der_string(der_buf *out, unsigned char tag, const char *s) {
    der_tlv(out, tag, s, strlen(s));
}
