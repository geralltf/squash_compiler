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

/* smallest possible 2D case: 2x4, fully specified, no NULL/shorthand rows */
static const Conv table[2][4] = {
    { F00, F01, F02, F03 },
    { F10, F11, F12, F13 },
};

int main(void) {
    dbgmark("--- flat dump, 2x4 fully-specified table ---");
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
