#include <stdio.h>
#include <string.h>
#include "android_bignum.h"

static void print_hex(const bignum *a, FILE *f) {
    int len = (bn_bitlen(a) + 7) / 8;
    unsigned char buf[300];
    int i;
    if (len == 0) { fprintf(f, "0"); return; }
    bn_to_bytes_be(a, buf, len);
    for (i = 0; i < len; i++) fprintf(f, "%02x", buf[i]);
}

static void bn_from_hex(bignum *a, const char *hex) {
    unsigned char buf[300];
    char padded[600];
    int hexlen = (int)strlen(hex);
    int len, i;
    if (hexlen % 2 != 0) {
        /* odd-length hex string: pad with a leading zero nibble so no
         * trailing digit gets silently truncated by integer division */
        padded[0] = '0';
        strcpy(padded + 1, hex);
        hex = padded;
        hexlen++;
    }
    len = hexlen / 2;
    for (i = 0; i < len; i++) sscanf(hex + i*2, "%2hhx", &buf[i]);
    bn_from_bytes_be(a, buf, len);
}

int main(void) {
    /* Basic arithmetic sanity */
    bignum a, b, r;
    bn_set_u32(&a, 123456789);
    bn_set_u32(&b, 987654321);
    bn_add(&r, &a, &b);
    printf("123456789 + 987654321 = "); print_hex(&r, stdout); printf(" (expect hex of 1111111110)\n");

    bn_mul(&r, &a, &b);
    printf("123456789 * 987654321 = "); print_hex(&r, stdout); printf("\n");

    /* Write a file of large random-ish hex values for python to compute
     * expected modexp/modinv results, which we then compare against. */
    {
        bignum base, exp, mod, modexp_result, inv_result;
        FILE *f;

        bn_from_hex(&base, "9a7b3c1d5e6f7089abcdef0123456789fedcba9876543210aabbccddeeff001");
        bn_from_hex(&exp,  "10001"); /* 65537 */
        bn_from_hex(&mod,  "c3a5e1f2d3b4a5968778655443322110ffeeddccbbaa99887766554433221105");

        bn_modexp(&modexp_result, &base, &exp, &mod);
        printf("modexp result = "); print_hex(&modexp_result, stdout); printf("\n");

        if (bn_modinv(&inv_result, &exp, &mod) == 0) {
            printf("modinv(0x10001, mod) = "); print_hex(&inv_result, stdout); printf("\n");
            /* verify: (exp * inv) mod mod == 1 */
            { bignum prod, check;
              bn_mul(&prod, &exp, &inv_result);
              bn_mod(&check, &prod, &mod);
              printf("self-check (exp*inv mod mod, expect 1) = "); print_hex(&check, stdout); printf("\n");
            }
        } else {
            printf("modinv: no inverse (unexpected)\n");
        }

        f = fopen("/tmp/claude-1000/-home-squash-projects-squash-compler-squash-compiler/3a439974-83aa-407c-b935-d7a0d8fca460/scratchpad/bignum_test_vectors.txt", "w");
        fprintf(f, "base=9a7b3c1d5e6f7089abcdef0123456789fedcba9876543210aabbccddeeff001\n");
        fprintf(f, "exp=10001\n");
        fprintf(f, "mod=c3a5e1f2d3b4a5968778655443322110ffeeddccbbaa99887766554433221105\n");
        fprintf(f, "modexp_result="); { char buf2[600]; int len=(bn_bitlen(&modexp_result)+7)/8; unsigned char bb[300]; bn_to_bytes_be(&modexp_result,bb,len); int i; for(i=0;i<len;i++) sprintf(buf2+i*2,"%02x",bb[i]); buf2[len*2]=0; fprintf(f,"%s\n",buf2); }
        fprintf(f, "modinv_result="); { char buf2[600]; int len=(bn_bitlen(&inv_result)+7)/8; unsigned char bb[300]; bn_to_bytes_be(&inv_result,bb,len); int i; for(i=0;i<len;i++) sprintf(buf2+i*2,"%02x",bb[i]); buf2[len*2]=0; fprintf(f,"%s\n",buf2); }
        fclose(f);
    }

    /* RSA-shape round trip with small (fast, hand-verifiable) numbers:
     * p=61, q=53 -> n=3233, phi=3120, e=17, d=2753 (textbook RSA example) */
    {
        bignum n, e, d, m, c, m2;
        bn_set_u32(&n, 3233);
        bn_set_u32(&e, 17);
        bn_set_u32(&d, 2753);
        bn_set_u32(&m, 65);
        bn_modexp(&c, &m, &e, &n);
        bn_modexp(&m2, &c, &d, &n);
        printf("textbook RSA: m=65 -> c=%u -> decrypt=%u (expect c=2790, decrypt=65)\n",
               c.len ? c.limb[0] : 0, m2.len ? m2.limb[0] : 0);
    }

    return 0;
}
