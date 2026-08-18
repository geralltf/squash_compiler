#include <stdio.h>
#include <windows.h>
static void dbgmark_hex(const char *label, unsigned long long v) {
    HANDLE h = GetStdHandle((DWORD)-11);
    DWORD written;
    char buf[128];
    int n = sprintf(buf, "%s=0x%llx\n", label, v);
    WriteFile(h, buf, (DWORD)n, &written, NULL);
}

typedef unsigned char mz_uint8;

typedef struct {
    mz_uint8 *m_pA, *m_pB;
    mz_uint8 m_output_buf[8192];
} Comp;

int main(void) {
    Comp c;
    Comp *d = &c;
    d->m_pB = d->m_output_buf + 0x1C60;
    mz_uint8 *pOutput_buf_start = d->m_output_buf;

    mz_uint8 *localA = d->m_pB;
    mz_uint8 *localB = pOutput_buf_start;
    dbgmark_hex("localA", (unsigned long long)(size_t)localA);
    dbgmark_hex("localB", (unsigned long long)(size_t)localB);
    int nlocal = (int)(localA - localB);
    dbgmark_hex("nlocal", (unsigned long long)(unsigned int)nlocal);

    int n = (int)(d->m_pB - pOutput_buf_start);
    dbgmark_hex("n", (unsigned long long)(unsigned int)n);
    return 0;
}
