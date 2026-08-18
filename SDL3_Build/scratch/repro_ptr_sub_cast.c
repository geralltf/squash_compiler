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

int main(void) {
    mz_uint8 buf[8192];
    mz_uint8 *pStart = buf;
    mz_uint8 *pOutput = buf + 0x1C60;
    int n;
    n = (int)(pOutput - pStart);
    dbgmark_hex("n", (unsigned long long)(unsigned int)n);
    return n;
}
