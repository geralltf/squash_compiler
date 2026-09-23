/* Schoolbook arbitrary-precision arithmetic -- see android_bignum.h for
 * scope/rationale. All operations here are O(n) or O(n^2) in the number of
 * limbs; this is a build-time signing tool (run once per APK), not a TLS
 * stack, so straightforward and auditable beats fast. */
#include "android_bignum.h"
#include <string.h>
#include <stdio.h>

void bn_zero(bignum *a) { memset(a->limb, 0, sizeof a->limb); a->len = 0; }

void bn_set_u32(bignum *a, uint32_t v) {
    bn_zero(a);
    if (v) { a->limb[0] = v; a->len = 1; }
}

void bn_copy(bignum *dst, const bignum *src) {
    memcpy(dst->limb, src->limb, sizeof dst->limb);
    dst->len = src->len;
}

int bn_is_zero(const bignum *a) { return a->len == 0; }

void bn_trim(bignum *a) {
    int i = BN_MAX_LIMBS - 1;
    while (i >= 0 && a->limb[i] == 0) i--;
    a->len = i + 1;
}

int bn_cmp(const bignum *a, const bignum *b) {
    if (a->len != b->len) return a->len < b->len ? -1 : 1;
    { int i; for (i = a->len - 1; i >= 0; i--) {
        if (a->limb[i] != b->limb[i]) return a->limb[i] < b->limb[i] ? -1 : 1;
    } }
    return 0;
}

void bn_add(bignum *r, const bignum *a, const bignum *b) {
    uint64_t carry = 0;
    int n = a->len > b->len ? a->len : b->len;
    int i;
    bignum tmp; bn_zero(&tmp);
    for (i = 0; i < n || carry; i++) {
        uint64_t av = (i < a->len) ? a->limb[i] : 0;
        uint64_t bv = (i < b->len) ? b->limb[i] : 0;
        uint64_t s = av + bv + carry;
        tmp.limb[i] = (uint32_t)s;
        carry = s >> 32;
    }
    bn_trim(&tmp);
    bn_copy(r, &tmp);
}

/* Requires a >= b (checked by caller; all call sites here maintain this). */
void bn_sub(bignum *r, const bignum *a, const bignum *b) {
    int64_t borrow = 0;
    int i;
    bignum tmp; bn_zero(&tmp);
    for (i = 0; i < a->len; i++) {
        int64_t av = a->limb[i];
        int64_t bv = (i < b->len) ? b->limb[i] : 0;
        int64_t d = av - bv - borrow;
        if (d < 0) { d += ((int64_t)1 << 32); borrow = 1; } else borrow = 0;
        tmp.limb[i] = (uint32_t)d;
    }
    bn_trim(&tmp);
    bn_copy(r, &tmp);
}

void bn_mul(bignum *r, const bignum *a, const bignum *b) {
    bignum tmp; bn_zero(&tmp);
    if (a->len == 0 || b->len == 0) { bn_copy(r, &tmp); return; }
    {
        int i, j;
        for (i = 0; i < a->len; i++) {
            uint64_t carry = 0;
            if (a->limb[i] == 0) continue;
            for (j = 0; j < b->len; j++) {
                uint64_t p = (uint64_t)a->limb[i] * (uint64_t)b->limb[j]
                             + (uint64_t)tmp.limb[i+j] + carry;
                tmp.limb[i+j] = (uint32_t)p;
                carry = p >> 32;
            }
            { int k = i + b->len; while (carry) { uint64_t s = (uint64_t)tmp.limb[k] + carry; tmp.limb[k] = (uint32_t)s; carry = s >> 32; k++; } }
        }
    }
    bn_trim(&tmp);
    bn_copy(r, &tmp);
}

void bn_shl1(bignum *a) {
    uint32_t carry = 0;
    int i;
    for (i = 0; i < a->len; i++) {
        uint32_t nc = a->limb[i] >> 31;
        a->limb[i] = (a->limb[i] << 1) | carry;
        carry = nc;
    }
    if (carry) { a->limb[a->len] = 1; a->len++; }
}

void bn_shr1(bignum *a) {
    uint32_t carry = 0;
    int i;
    for (i = a->len - 1; i >= 0; i--) {
        uint32_t nc = a->limb[i] & 1;
        a->limb[i] = (a->limb[i] >> 1) | (carry << 31);
        carry = nc;
    }
    bn_trim(a);
}

int bn_bitlen(const bignum *a) {
    if (a->len == 0) return 0;
    {
        uint32_t top = a->limb[a->len - 1];
        int bits = (a->len - 1) * 32;
        while (top) { bits++; top >>= 1; }
        return bits;
    }
}

int bn_bit(const bignum *a, int i) {
    int limb_i = i / 32, bit_i = i % 32;
    if (limb_i >= a->len) return 0;
    return (a->limb[limb_i] >> bit_i) & 1;
}

void bn_divmod(bignum *q, bignum *r, const bignum *a, const bignum *b) {
    /* Bit-by-bit restoring division: shift each bit of `a` (MSB first)
     * into a running remainder, subtracting `b` whenever it fits. Simple
     * and easy to verify correct; see android_bignum.h's top comment for
     * why this (rather than Knuth's word-at-a-time algorithm D, or
     * Montgomery reduction) is an acceptable tradeoff here. */
    bignum rem, quot;
    int nbits = bn_bitlen(a);
    int i;
    bn_zero(&rem);
    bn_zero(&quot);
    for (i = nbits - 1; i >= 0; i--) {
        bn_shl1(&rem);
        if (bn_bit(a, i)) {
            if (rem.len == 0) { rem.limb[0] = 1; rem.len = 1; }
            else rem.limb[0] |= 1;
        }
        if (bn_cmp(&rem, b) >= 0) {
            bn_sub(&rem, &rem, b);
            /* set bit i of quotient */
            { int limb_i = i / 32, bit_i = i % 32;
              quot.limb[limb_i] |= (1u << bit_i);
              if (limb_i + 1 > quot.len) quot.len = limb_i + 1; }
        }
    }
    bn_trim(&quot);
    if (q) bn_copy(q, &quot);
    if (r) bn_copy(r, &rem);
}

void bn_mod(bignum *r, const bignum *a, const bignum *m) {
    bn_divmod(NULL, r, a, m);
}

void bn_modexp(bignum *r, const bignum *base, const bignum *exp, const bignum *mod) {
    bignum result, b, e;
    bn_set_u32(&result, 1);
    bn_mod(&b, base, mod);
    bn_copy(&e, exp);
    while (!bn_is_zero(&e)) {
        if (bn_bit(&e, 0)) {
            bignum t; bn_mul(&t, &result, &b); bn_mod(&result, &t, mod);
        }
        { bignum t; bn_mul(&t, &b, &b); bn_mod(&b, &t, mod); }
        bn_shr1(&e);
    }
    bn_copy(r, &result);
}

/* ---- signed bignum helper, used only by bn_modinv's extended Euclid ---- */
typedef struct { bignum mag; int neg; /* neg is meaningless when mag==0 */ } sbignum;

static void sb_from_bn(sbignum *s, const bignum *a) { bn_copy(&s->mag, a); s->neg = 0; }

static void sb_sub(sbignum *r, const sbignum *a, const sbignum *b) {
    /* r = a - b, all combinations of signs, via magnitude add/sub. */
    if (a->neg == b->neg) {
        if (bn_cmp(&a->mag, &b->mag) >= 0) {
            bn_sub(&r->mag, &a->mag, &b->mag);
            r->neg = bn_is_zero(&r->mag) ? 0 : a->neg;
        } else {
            bn_sub(&r->mag, &b->mag, &a->mag);
            r->neg = bn_is_zero(&r->mag) ? 0 : !a->neg;
        }
    } else {
        bn_add(&r->mag, &a->mag, &b->mag);
        r->neg = bn_is_zero(&r->mag) ? 0 : a->neg;
    }
}

static void sb_mul(sbignum *r, const sbignum *a, const sbignum *b) {
    bn_mul(&r->mag, &a->mag, &b->mag);
    r->neg = bn_is_zero(&r->mag) ? 0 : (a->neg != b->neg);
}

int bn_modinv(bignum *r, const bignum *a, const bignum *m) {
    /* Standard iterative extended Euclidean algorithm (see
     * android_bignum.h's modinv doc comment for the bound argument on why
     * BN_MAX_LIMBS's headroom is enough for the signed t/newt coefficients
     * here, which are bounded in magnitude by `m`). */
    bignum rr, newr, q, tmp;
    sbignum t, newt, qs, prod, diff;

    bn_copy(&rr, m);
    bn_mod(&newr, a, m);
    bn_set_u32(&t.mag, 0); t.neg = 0;
    bn_set_u32(&newt.mag, 1); newt.neg = 0;

    while (!bn_is_zero(&newr)) {
        bn_divmod(&q, &tmp, &rr, &newr);
        sb_from_bn(&qs, &q);

        sb_mul(&prod, &qs, &newt);
        sb_sub(&diff, &t, &prod);
        t = newt;
        newt = diff;

        bn_copy(&rr, &newr);
        bn_copy(&newr, &tmp);
    }

    { bignum one; bn_set_u32(&one, 1);
      if (bn_cmp(&rr, &one) != 0) return -1; /* not invertible */
    }

    if (t.neg) {
        bignum res;
        bn_sub(&res, m, &t.mag); /* t.mag < m always holds once reduced below */
        bn_mod(&res, &res, m);
        bn_copy(r, &res);
    } else {
        bn_mod(r, &t.mag, m);
    }
    return 0;
}

void bn_to_bytes_be(const bignum *a, unsigned char *out_bytes, int len) {
    int i;
    memset(out_bytes, 0, (size_t)len);
    for (i = 0; i < len; i++) {
        int limb_i = i / 4, byte_i = i % 4;
        uint32_t v = (limb_i < a->len) ? a->limb[limb_i] : 0;
        out_bytes[len - 1 - i] = (unsigned char)(v >> (byte_i * 8));
    }
}

void bn_from_bytes_be(bignum *a, const unsigned char *bytes, int len) {
    int i;
    bn_zero(a);
    for (i = 0; i < len; i++) {
        int limb_i = i / 4, byte_i = i % 4;
        a->limb[limb_i] |= ((uint32_t)bytes[len - 1 - i]) << (byte_i * 8);
    }
    bn_trim(a);
}

void bn_random_odd_topbit(bignum *a, int nbits) {
    int nbytes = (nbits + 7) / 8;
    unsigned char buf[520]; /* comfortably covers a 4096-bit prime candidate */
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || nbytes > (int)sizeof buf) { bn_zero(a); return; }
    if (fread(buf, 1, (size_t)nbytes, f) != (size_t)nbytes) { fclose(f); bn_zero(a); return; }
    fclose(f);
    /* buf[0] is the most-significant byte (big-endian). Clear whatever
     * high bits fall outside the requested width, then force exactly bit
     * (nbits-1) on -- this guarantees the value has precisely nbits bits,
     * never fewer. Also force the bottom bit on (odd -- a necessary
     * condition for primality beyond 2). */
    { int extra_bits = nbytes * 8 - nbits;
      buf[0] = (unsigned char)(buf[0] & (0xFFu >> extra_bits));
      buf[0] = (unsigned char)(buf[0] | (1u << (7 - extra_bits))); }
    buf[nbytes - 1] |= 1;
    bn_from_bytes_be(a, buf, nbytes);
}

/* First 400 primes (up to ~2740), computed once via simple trial division.
 * Used as a fast pre-filter in bn_is_probable_prime: the overwhelming
 * majority of random odd candidates are composite with a small factor, and
 * rejecting those via a handful of cheap native-width mod operations is
 * orders of magnitude cheaper than a full Miller-Rabin round (a ~1024-bit
 * modexp) -- without this, RSA key generation is dominated by wasting
 * expensive modexps on candidates a one-line mod check would have caught. */
static int get_small_primes(const uint32_t **out) {
    static uint32_t primes[400];
    static int n = -1;
    if (n < 0) {
        uint32_t candidate = 2;
        n = 0;
        while (n < 400) {
            int isp = 1, j;
            for (j = 0; j < n; j++) {
                if ((uint64_t)primes[j] * primes[j] > candidate) break;
                if (candidate % primes[j] == 0) { isp = 0; break; }
            }
            if (isp) primes[n++] = candidate;
            candidate++;
        }
    }
    *out = primes;
    return n;
}

/* a mod p for a native uint32_t p, via Horner's method in base 2^32 --
 * avoids a full bignum division for a single-limb modulus. */
static uint32_t bn_mod_u32(const bignum *a, uint32_t p) {
    uint64_t rem = 0;
    int i;
    for (i = a->len - 1; i >= 0; i--) {
        rem = ((rem << 32) | (uint64_t)a->limb[i]) % p;
    }
    return (uint32_t)rem;
}

int bn_is_probable_prime(const bignum *a, int rounds) {
    bignum one, two, three, a_minus_1, d;
    int r, i;

    bn_set_u32(&one, 1);
    bn_set_u32(&two, 2);
    bn_set_u32(&three, 3);
    if (bn_cmp(a, &two) < 0) return 0;
    if (bn_cmp(a, &two) == 0 || bn_cmp(a, &three) == 0) return 1;
    if (!(a->limb[0] & 1)) return 0; /* even */

    { const uint32_t *primes; int nprimes = get_small_primes(&primes);
      for (i = 0; i < nprimes; i++) {
          uint32_t p = primes[i];
          bignum bp; bn_set_u32(&bp, p);
          if (bn_cmp(a, &bp) == 0) return 1; /* a IS this small prime */
          if (bn_mod_u32(a, p) == 0) return 0; /* small factor found */
      }
    }

    bn_sub(&a_minus_1, a, &one);
    bn_copy(&d, &a_minus_1);
    r = 0;
    while (!bn_is_zero(&d) && (d.limb[0] & 1) == 0) { bn_shr1(&d); r++; }

    for (i = 0; i < rounds; i++) {
        bignum witness, x;
        int nbits = bn_bitlen(a);
        int witness_ok = 0;
        int tries;
        for (tries = 0; tries < 10 && !witness_ok; tries++) {
            bn_random_odd_topbit(&witness, nbits);
            bn_mod(&witness, &witness, a);
            if (bn_cmp(&witness, &two) >= 0 && bn_cmp(&witness, &a_minus_1) < 0) witness_ok = 1;
        }
        if (!witness_ok) { bn_copy(&witness, &two); }

        bn_modexp(&x, &witness, &d, a);
        if (bn_cmp(&x, &one) == 0 || bn_cmp(&x, &a_minus_1) == 0) continue;

        { int j; int composite = 1;
          for (j = 0; j < r - 1; j++) {
              bignum xt; bn_mul(&xt, &x, &x); bn_mod(&x, &xt, a);
              if (bn_cmp(&x, &a_minus_1) == 0) { composite = 0; break; }
          }
          if (composite) return 0;
        }
    }
    return 1;
}
