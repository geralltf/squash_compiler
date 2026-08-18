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
static void F00(void) {} static void F01(void) {} static void F02(void) {} static void F03(void) {}
static void F10(void) {} static void F11(void) {} static void F12(void) {} static void F13(void) {}

static Conv table[2][4];  /* not const, not brace-initialized -- plain runtime stores */

int main(void) {
    table[0][0] = F00;
    table[0][1] = F01;
    table[0][2] = F02;
    table[0][3] = F03;
    table[1][0] = F10;
    table[1][1] = F11;
    table[1][2] = F12;
    table[1][3] = F13;

    dbgmark("--- explicit runtime stores, then flat dump ---");
    const Conv *flat = (const Conv *)table;
    int i;
    for (i = 0; i < 8; i++) {
        dbgmark_ptr("flat[i]", (unsigned long long)(size_t)flat[i]);
    }
    dbgmark_ptr("expect F00", (unsigned long long)(size_t)F00);
    dbgmark_ptr("expect F01", (unsigned long long)(size_t)F01);
    dbgmark_ptr("expect F02", (unsigned long long)(size_t)F02);
    dbgmark_ptr("expect F03", (unsigned long long)(size_t)F03);
    dbgmark_ptr("expect F10", (unsigned long long)(size_t)F10);
    dbgmark_ptr("expect F11", (unsigned long long)(size_t)F11);
    dbgmark_ptr("expect F12", (unsigned long long)(size_t)F12);
    dbgmark_ptr("expect F13", (unsigned long long)(size_t)F13);
    return 0;
}
