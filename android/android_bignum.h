#ifndef ANDROID_BIGNUM_H
#define ANDROID_BIGNUM_H
#include <stdint.h>
#include <stddef.h>

/* Minimal arbitrary-precision unsigned integer library, sized generously
 * (6400 bits) to cover 2048-bit RSA operations plus the transient
 * intermediate blow-up that happens inside the signed extended-Euclidean
 * step of modular inverse (see android_rsa.c) before it settles back down
 * to its final, much smaller, Bezout-bounded result. This is a build-time
 * tool (run once per APK signed), so schoolbook algorithms (no Montgomery
 * reduction, no Karatsuba) are used throughout for simplicity/auditability
 * over raw speed. */
#define BN_MAX_LIMBS 200 /* 200 * 32 = 6400 bits */

typedef struct {
    uint32_t limb[BN_MAX_LIMBS]; /* limb[0] = least significant */
    int len; /* number of significant limbs; 0 means the value zero */
} bignum;

void bn_zero(bignum *a);
void bn_set_u32(bignum *a, uint32_t v);
void bn_copy(bignum *dst, const bignum *src);
int  bn_is_zero(const bignum *a);
int  bn_cmp(const bignum *a, const bignum *b); /* -1, 0, 1 */
void bn_trim(bignum *a); /* recompute ->len after limb[] is mutated directly */

void bn_add(bignum *r, const bignum *a, const bignum *b);
/* Requires a >= b. */
void bn_sub(bignum *r, const bignum *a, const bignum *b);
void bn_mul(bignum *r, const bignum *a, const bignum *b);
void bn_shl1(bignum *a);
void bn_shr1(bignum *a);
int  bn_bit(const bignum *a, int i);      /* value of bit i (0 = LSB) */
int  bn_bitlen(const bignum *a);          /* index of highest set bit + 1; 0 for zero */

/* q = a / b, r = a % b (schoolbook bit-by-bit long division). q or r may be NULL. */
void bn_divmod(bignum *q, bignum *r, const bignum *a, const bignum *b);
void bn_mod(bignum *r, const bignum *a, const bignum *m);

/* r = base^exp mod mod, via binary square-and-multiply. */
void bn_modexp(bignum *r, const bignum *base, const bignum *exp, const bignum *mod);

/* r = a^-1 mod m. Returns 0 on success, -1 if a has no inverse mod m
 * (gcd(a,m) != 1) -- the caller (RSA keygen) must reject/retry in that case. */
int bn_modinv(bignum *r, const bignum *a, const bignum *m);

/* Fills `out_bytes` with a big-endian, zero-padded encoding of `a` of
 * exactly `len` bytes (used for RSA modulus/signature I-O, which are
 * always a fixed byte width). Truncates silently if `a` doesn't fit --
 * callers size `len` generously enough that this never happens for our
 * actual key sizes. */
void bn_to_bytes_be(const bignum *a, unsigned char *out_bytes, int len);
void bn_from_bytes_be(bignum *a, const unsigned char *bytes, int len);

/* Fills `a` with `nbits` uniformly random bits (top bit and bottom bit
 * both forced to 1 -- the standard shape for an RSA prime candidate),
 * reading entropy from /dev/urandom. */
void bn_random_odd_topbit(bignum *a, int nbits);

/* Miller-Rabin probabilistic primality test, `rounds` independent random
 * witnesses (20 rounds gives a false-positive probability under 2^-40,
 * comfortably below what matters for a build-time RSA key). */
int bn_is_probable_prime(const bignum *a, int rounds);

#endif
