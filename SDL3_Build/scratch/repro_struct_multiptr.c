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
    mz_uint8 *m_pLZ_code_buf, *m_pLZ_flags, *m_pOutput_buf, *m_pOutput_buf_end;
    mz_uint8 m_output_buf[8192];
} Comp;

int main(void) {
    Comp c;
    Comp *d = &c;
    d->m_pOutput_buf = d->m_output_buf + 0x1C60;
    mz_uint8 *pOutput_buf_start = d->m_output_buf;

    dbgmark_hex("d->m_pOutput_buf", (unsigned long long)(size_t)d->m_pOutput_buf);
    dbgmark_hex("pOutput_buf_start", (unsigned long long)(size_t)pOutput_buf_start);
    int n = (int)(d->m_pOutput_buf - pOutput_buf_start);
    dbgmark_hex("n", (unsigned long long)(unsigned int)n);
    return 0;
}
