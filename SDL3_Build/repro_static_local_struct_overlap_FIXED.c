#include <windows.h>
typedef struct { int error; char *str; unsigned long long len; } Info;
typedef struct { Info info[2]; int current; } ErrT;
static ErrT *get_static(void) {
    static ErrT the_struct;
    static char str1[128];
    static char str2[128];
    the_struct.info[0].str = str1;
    the_struct.info[0].len = sizeof(str1);
    the_struct.info[1].str = str2;
    the_struct.info[1].len = sizeof(str2);
    return &the_struct;
}

static int my_strlen(const char *s) { int n=0; while (s[n]) n++; return n; }
static void dbgmark(const char *s) {
    HANDLE h = GetStdHandle((DWORD)-11);
    DWORD written;
    WriteFile(h, s, (DWORD)my_strlen(s), &written, NULL);
    WriteFile(h, "\n", 1, &written, NULL);
}
static void dbgnum(const char *label, unsigned long long v) {
    char buf[64];
    int n = 0;
    for (int i = 0; label[i]; i++) buf[n++] = label[i];
    buf[n++]='=';
    buf[n++]='0';
    buf[n++]='x';
    char digits[32];
    int nd = 0;
    if (v == 0) { digits[nd] = '0'; nd = nd + 1; }
    while (v) { digits[nd] = "0123456789abcdef"[v % 16]; nd = nd + 1; v = v / 16; }
    int i = nd - 1;
    while (i >= 0) { buf[n] = digits[i]; n = n + 1; i = i - 1; }
    buf[n] = 0;
    dbgmark(buf);
}

int main(void) {
    ErrT *e = get_static();
    Info *info0 = &e->info[0];
    Info *info1 = &e->info[1];
    unsigned long long struct_addr = (unsigned long long)e;
    unsigned long long str1_addr = (unsigned long long)info0->str;
    unsigned long long str2_addr = (unsigned long long)info1->str;
    dbgnum("struct_addr", struct_addr);
    dbgnum("str1_addr", str1_addr);
    dbgnum("str2_addr", str2_addr);
    dbgnum("struct_size", (unsigned long long)sizeof(ErrT));
    return 0;
}
