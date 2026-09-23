#ifndef ANDROID_DER_H
#define ANDROID_DER_H
#include <stdint.h>
#include <stddef.h>
#include "android_bignum.h"

/* Minimal growable byte buffer, shared by the DER encoder and its callers
 * (android_rsa.c's SPKI export, android_x509.c's certificate builder). */
typedef struct {
    unsigned char *data;
    size_t len, cap;
} der_buf;

void der_buf_init(der_buf *b);
void der_buf_free(der_buf *b);
void der_buf_append(der_buf *b, const void *p, size_t n);

/* Writes one DER TLV: tag byte, DER-encoded length, then `content_len`
 * bytes copied from `content`. */
void der_tlv(der_buf *out, unsigned char tag, const void *content, size_t content_len);

/* Wraps the bytes already in `inner` (which the caller has been building
 * up as a der_buf) in a SEQUENCE (0x30) tag+length, appending the result
 * to `out`. Does not free/clear `inner`. */
void der_sequence(der_buf *out, const der_buf *inner);

/* Encodes an unsigned bignum as a DER INTEGER (0x02): prepends a 0x00 byte
 * if the value's top bit is set, so it isn't misread as negative under
 * DER's two's-complement INTEGER convention. */
void der_integer_from_bignum(der_buf *out, const bignum *v);

/* Small non-negative integer (e.g. an X.509 version field, serial number
 * that fits in a native int). */
void der_integer_u32(der_buf *out, uint32_t v);

/* NULL (0x05 0x00). */
void der_null(der_buf *out);

/* Raw OID bytes (caller supplies the already-DER-encoded OID content,
 * e.g. {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01} for rsaEncryption)
 * wrapped in an OBJECT IDENTIFIER (0x06) tag+length. */
void der_oid(der_buf *out, const unsigned char *oid_bytes, size_t oid_len);

/* BIT STRING (0x03) with a 0x00 "no unused bits" byte prefix, wrapping
 * `content_len` bytes of already-DER-encoded content (typically another
 * SEQUENCE, e.g. RSAPublicKey inside a SubjectPublicKeyInfo). */
void der_bitstring(der_buf *out, const void *content, size_t content_len);

/* UTF8String (0x0c) / PrintableString (0x13) / raw OCTET STRING (0x04) --
 * all share the same tag+length+bytes shape, only the tag differs. */
void der_string(der_buf *out, unsigned char tag, const char *s);

#endif
