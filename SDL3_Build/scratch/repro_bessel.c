/* Isolates BesselI0's exact "while (t >= sum * SDL_FLT_EPSILON)" float
 * loop condition (real SDL3 source, SDL_audioresample.c) to check whether
 * the codegen_branch float-comparison fix makes it converge correctly (a
 * few dozen iterations, as in real C) or loop forever (a remaining bug in
 * the new codegen). */
#include <windows.h>
#include <stdio.h>
#include <float.h>

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

#define MY_FLT_EPSILON 1.1920929e-7f

static float BesselI0(float x)
{
    float sum = 0.0f;
    float i = 1.0f;
    float t = 1.0f;
    x *= x * 0.25f;

    int iters = 0;
    while (t >= sum * MY_FLT_EPSILON) {
        sum += t;
        t *= x / (i * i);
        ++i;
        iters++;
        if (iters > 2000) {
            dbgmark("GENUINE INFINITE LOOP -- stopping after 2000 iterations");
            dbgmark_ptr("t", (unsigned long long)*(unsigned int*)&t);
            dbgmark_ptr("sum", (unsigned long long)*(unsigned int*)&sum);
            return sum;
        }
    }
    dbgmark_ptr("converged after iters", (unsigned long long)iters);
    return sum;
}

int main(void) {
    dbgmark("start");
    /* real call site: BesselI0(beta), beta is a small constant (Kaiser
     * window beta parameter, typically ~5-ish for SDL3's resampler) */
    float result = BesselI0(5.0f);
    dbgmark_ptr("BesselI0(5.0) result bits", (unsigned long long)*(unsigned int*)&result);
    dbgmark("done");
    return 0;
}
