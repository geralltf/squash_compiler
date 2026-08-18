/* Test: function call with double at position 1 (Win64 ABI: must go in XMM1) */
extern int printf(const char *fmt, ...);
extern void *malloc(int size);
extern void free(void *p);

double store;

void store_double(void *ctx, double val) {
    store = val;
}

int main(int argc, char **argv) {
    void *fake_ctx = malloc(8);
    store_double(fake_ctx, 3.14);
    int truncated = (int)store;
    if (truncated == 3) {
        printf("PASS: store_double got %.2f\n", store);
    } else {
        printf("FAIL: store_double got %d (expected 3)\n", truncated);
    }
    free(fake_ctx);
    return 0;
}
