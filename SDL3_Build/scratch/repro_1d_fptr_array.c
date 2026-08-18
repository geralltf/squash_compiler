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

typedef void (*Conv)(void);
static void F0(void) { dbgmark("F0"); }
static void F1(void) { dbgmark("F1"); }
static void F2(void) { dbgmark("F2"); }

static const Conv table1d[3] = { F0, F1, F2 };

int main(void) {
    dbgmark("--- 1D array of function pointers ---");
    dbgmark_ptr("table1d[0]", (unsigned long long)(size_t)table1d[0]);
    dbgmark_ptr("expect F0 ", (unsigned long long)(size_t)F0);
    dbgmark_ptr("table1d[1]", (unsigned long long)(size_t)table1d[1]);
    dbgmark_ptr("expect F1 ", (unsigned long long)(size_t)F1);
    dbgmark_ptr("table1d[2]", (unsigned long long)(size_t)table1d[2]);
    dbgmark_ptr("expect F2 ", (unsigned long long)(size_t)F2);
    return 0;
}
