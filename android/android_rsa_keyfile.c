#include "android_rsa_keyfile.h"
#include <stdio.h>
#include <stdlib.h>

static void write_u32(FILE *f, uint32_t v) {
    unsigned char le[4] = { (unsigned char)v, (unsigned char)(v>>8), (unsigned char)(v>>16), (unsigned char)(v>>24) };
    fwrite(le, 1, 4, f);
}
static uint32_t read_u32(FILE *f) {
    unsigned char le[4];
    if (fread(le, 1, 4, f) != 4) return 0xFFFFFFFFu;
    return (uint32_t)le[0] | ((uint32_t)le[1]<<8) | ((uint32_t)le[2]<<16) | ((uint32_t)le[3]<<24);
}

static void write_bignum(FILE *f, const bignum *v) {
    int nbytes = (bn_bitlen(v) + 7) / 8;
    unsigned char buf[300];
    write_u32(f, (uint32_t)nbytes);
    if (nbytes > 0) { bn_to_bytes_be(v, buf, nbytes); fwrite(buf, 1, (size_t)nbytes, f); }
}

static int read_bignum(FILE *f, bignum *v) {
    uint32_t nbytes = read_u32(f);
    unsigned char buf[300];
    if (nbytes == 0xFFFFFFFFu || nbytes > sizeof buf) return -1;
    if (nbytes > 0 && fread(buf, 1, nbytes, f) != nbytes) return -1;
    bn_from_bytes_be(v, buf, (int)nbytes);
    return 0;
}

int android_rsa_save(const android_rsa_key *key, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    write_u32(f, (uint32_t)key->bits);
    write_bignum(f, &key->n);
    write_bignum(f, &key->e);
    write_bignum(f, &key->d);
    fclose(f);
    return 0;
}

int android_rsa_load(android_rsa_key *key, const char *path) {
    FILE *f = fopen(path, "rb");
    uint32_t bits;
    if (!f) return -1;
    bits = read_u32(f);
    if (bits == 0xFFFFFFFFu) { fclose(f); return -1; }
    key->bits = (int)bits;
    if (read_bignum(f, &key->n) != 0 || read_bignum(f, &key->e) != 0 || read_bignum(f, &key->d) != 0) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}
