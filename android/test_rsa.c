#include <stdio.h>
#include <string.h>
#include <time.h>
#include "android_rsa.h"

int main(void) {
    android_rsa_key key;
    const char *message = "hello squash android signing test";
    unsigned char sig[256]; /* 2048 bits / 8 */
    unsigned char *spki_der;
    size_t spki_len;
    FILE *f;
    clock_t t0, t1;

    printf("generating 2048-bit RSA key...\n");
    t0 = clock();
    android_rsa_generate(&key, 2048);
    t1 = clock();
    printf("keygen took %.2f seconds\n", (double)(t1 - t0) / CLOCKS_PER_SEC);

    android_rsa_sign_sha256(&key, message, strlen(message), sig);

    android_rsa_export_spki_der(&key, &spki_der, &spki_len);

    f = fopen("/tmp/claude-1000/-home-squash-projects-squash-compler-squash-compiler/3a439974-83aa-407c-b935-d7a0d8fca460/scratchpad/test_pubkey.der", "wb");
    fwrite(spki_der, 1, spki_len, f);
    fclose(f);

    f = fopen("/tmp/claude-1000/-home-squash-projects-squash-compler-squash-compiler/3a439974-83aa-407c-b935-d7a0d8fca460/scratchpad/test_sig.bin", "wb");
    fwrite(sig, 1, sizeof sig, f);
    fclose(f);

    f = fopen("/tmp/claude-1000/-home-squash-projects-squash-compler-squash-compiler/3a439974-83aa-407c-b935-d7a0d8fca460/scratchpad/test_message.txt", "wb");
    fwrite(message, 1, strlen(message), f);
    fclose(f);

    printf("wrote pubkey (%zu bytes), signature (%zu bytes), message\n", spki_len, sizeof sig);
    return 0;
}
