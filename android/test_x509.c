#include <stdio.h>
#include "android_rsa.h"
#include "android_x509.h"

int main(void) {
    android_rsa_key key;
    unsigned char *cert_der;
    size_t cert_len;
    FILE *f;

    printf("generating key...\n");
    android_rsa_generate(&key, 2048);

    android_x509_self_signed(&key, "squash", 1, 10000, &cert_der, &cert_len);

    f = fopen("/tmp/claude-1000/-home-squash-projects-squash-compler-squash-compiler/3a439974-83aa-407c-b935-d7a0d8fca460/scratchpad/test_cert.der", "wb");
    fwrite(cert_der, 1, cert_len, f);
    fclose(f);
    printf("wrote %zu byte certificate\n", cert_len);
    return 0;
}
