/* Minimal isolation of the SDL3 audio-hang root cause: "(*ptr).field2"
 * reads back field1's value when a PRIOR expression already accessed
 * "(*ptr).field1" via the same dereference-then-dot syntax on the same
 * pointer, in the same statement/expression tree. Found by decomposing
 * SDL_AUDIO_FRAMESIZE(x) == ((SDL_AUDIO_BYTESIZE((x).format)) * (x).channels)
 * where x is "*spec" -- (x).format is read first, then (x).channels reads
 * back .format's value again instead of .channels'. */
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

typedef struct { unsigned int field1; int field2; int field3; } TwoFields;

int main(void) {
    TwoFields t;
    t.field1 = 0x8120;
    t.field2 = 1;
    t.field3 = 8000;
    TwoFields *p = &t;

    dbgmark("--- test A: two separate statements ---");
    unsigned int a = (*p).field1;
    int b = (*p).field2;
    dbgmark_ptr("a (expect 0x8120)", (unsigned long long)a);
    dbgmark_ptr("b (expect 1)", (unsigned long long)b);

    dbgmark("--- test B: one expression, both accessed ---");
    unsigned long long combined = (unsigned long long)((*p).field1) * 1000 + (unsigned long long)((*p).field2);
    dbgmark_ptr("combined (expect 0x8120*1000+1)", combined);

    dbgmark("--- test C: exact macro shape (bytesize-like then channels-like) ---");
    unsigned long long r = ((((*p).field1) & 0xFFu) / 8) * (*p).field2;
    dbgmark_ptr("r (expect 4)", r);

    dbgmark("--- test D: arrow syntax instead of (*p). ---");
    unsigned int a2 = p->field1;
    int b2 = p->field2;
    dbgmark_ptr("a2 (expect 0x8120)", (unsigned long long)a2);
    dbgmark_ptr("b2 (expect 1)", (unsigned long long)b2);

    dbgmark("--- test E: field3 too, still deref-dot ---");
    int c = (*p).field3;
    dbgmark_ptr("c (expect 8000)", (unsigned long long)c);

    dbgmark("--- test F: deref-dot field2 with NO prior field1 access ---");
    TwoFields t2; t2.field1 = 0xAAAA; t2.field2 = 42; t2.field3 = 99;
    TwoFields *p2 = &t2;
    int b3 = (*p2).field2;
    dbgmark_ptr("b3 (expect 42)", (unsigned long long)b3);

    return 0;
}
