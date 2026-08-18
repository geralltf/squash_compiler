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
typedef int (*put_func)(const void*, int, void*);

typedef struct {
    mz_uint8 m_output_buf[8192];
    mz_uint8 *m_pOutput_buf;
    put_func m_pPut_buf_func;
    void *m_pOut_buf_size;
} Comp;

int main(void) {
    Comp c;
    Comp *d = &c;
    d->m_pPut_buf_func = (put_func)1; /* non-null, just a marker */
    d->m_pOut_buf_size = 0;
    d->m_pOutput_buf = d->m_output_buf + 0x1C60;

    mz_uint8 *pOutput_buf_start = ((d->m_pPut_buf_func == NULL) && ((*(size_t*)d->m_pOut_buf_size) >= 1000)) ? ((mz_uint8 *)d->m_pOut_buf_size) : d->m_output_buf;

    int n;
    n = (int)(d->m_pOutput_buf - pOutput_buf_start);
    dbgmark_hex("n", (unsigned long long)(unsigned int)n);
    return 0;
}
