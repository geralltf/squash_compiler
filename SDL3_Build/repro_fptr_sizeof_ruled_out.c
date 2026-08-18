#include <stdio.h>
#include <stdlib.h>

typedef struct { int error; char *str; unsigned long long len; } Info;
typedef void *(*ReallocFn)(void *, unsigned long long);
typedef struct {
    Info info[2];
    int current;
    ReallocFn realloc_func;
    void (*free_func)(void *);
} ErrT;

static unsigned long long g_received_size = 0;
static void *my_realloc(void *p, unsigned long long size) {
    g_received_size = size;
    return malloc((size_t)size);
}

int main(void) {
    ReallocFn fn = my_realloc;
    printf("sizeof(ErrT) at call site = %llu\n", (unsigned long long)sizeof(ErrT));

    ErrT *e;
    unsigned long long sz = sizeof(*e);
    printf("sz variable = %llu\n", sz);
    e = (ErrT *)fn(NULL, sz);

    printf("size actually received by callee = %llu\n", g_received_size);
    printf("e (returned ptr) = %p\n", (void*)e);

    /* Also test the DIRECT inline form: fn(NULL, sizeof(*e)) */
    g_received_size = 0;
    ErrT *e2 = (ErrT *)fn(NULL, sizeof(*e2));
    printf("size received (inline sizeof(*e2) call) = %llu\n", g_received_size);

    return 0;
}
