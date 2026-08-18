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
    mz_uint8 dummy[64];
    mz_uint8 *m_pOutput_buf;
} Comp;

int main(void) {
    Comp c;
    Comp *d = &c;
    mz_uint8 buf[8192];
    d->m_pOutput_buf = buf + 0x1C60;
    mz_uint8 *pOutput_buf_start = buf;

    int n = (int)(d->m_pOutput_buf - pOutput_buf_start);
    dbgmark_hex("n", (unsigned long long)(unsigned int)n);
    return 0;
}
