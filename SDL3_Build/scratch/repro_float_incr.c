#include <windows.h>
#include <stdio.h>

static void dbgmark(const char *s) {
    HANDLE h = GetStdHandle((DWORD)-11);
    DWORD written;
    WriteFile(h, s, (DWORD)strlen(s), &written, NULL);
    WriteFile(h, "\n", 1, &written, NULL);
}
static void dbgmark_ptr(const char *label, unsigned long long v) {
    char buf[64]; int p = 0; const char *lbl = label;
    while (*lbl) buf[p++] = *lbl++;
    buf[p++] = '='; buf[p++] = '0'; buf[p++] = 'x';
    int shift;
    for (shift = 60; shift >= 0; shift -= 4) {
        int nib = (int)((v >> shift) & 0xf);
        buf[p++] = (nib < 10) ? ('0' + nib) : ('a' + (nib - 10));
    }
    buf[p] = 0;
    dbgmark(buf);
}

int main(void) {
    dbgmark("start");
    float i = 1.0f;
    ++i;
    dbgmark_ptr("after ++i, expect 2.0 (0x40000000)", (unsigned long long)*(unsigned int*)&i);
    ++i;
    dbgmark_ptr("after ++i again, expect 3.0 (0x40400000)", (unsigned long long)*(unsigned int*)&i);
    i++;
    dbgmark_ptr("after i++, expect 4.0 (0x40800000)", (unsigned long long)*(unsigned int*)&i);
    return 0;
}
