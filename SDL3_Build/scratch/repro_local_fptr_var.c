/* Third bug in the same family: a LOCAL variable of a typedef'd
 * function-pointer type. Real SDL3's ConvertAudio() does exactly this:
 *   SDL_AudioChannelConverter channel_converter;
 *   channel_converter = channel_converters[a][b];
 *   channel_converter(...);
 * If typeinfo_size() (still not typedef-aware) is used anywhere in the
 * LOCAL variable stack-slot-size/store-width/load-width path the same way
 * it was for array elements, this local only gets a 4-byte slot and the
 * upper 32 bits of the real function address are lost on store or load,
 * so calling through it jumps to a truncated (near-NULL, or in this case
 * a low, spuriously "in-module-looking" but wrong) address. */
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

typedef void (*Conv)(int a, int b);
static void RealFunc(int a, int b) {
    dbgmark_ptr("RealFunc called, a", (unsigned long long)a);
    dbgmark_ptr("RealFunc called, b", (unsigned long long)b);
}

static void OtherFunc(int a, int b) {
    dbgmark_ptr("OtherFunc called, a", (unsigned long long)a);
    dbgmark_ptr("OtherFunc called, b", (unsigned long long)b);
}

static const Conv table2d[2][2] = {
    { RealFunc, OtherFunc },
    { OtherFunc, RealFunc },
};

int main(void) {
    dbgmark("start");
    /* exact real-code shape: index computed from two int locals, into a
     * 2D typedef'd-function-pointer array, assigned to a local var, then
     * called through -- matches SDL_audiocvt.c's ConvertAudio() exactly
     * (channel_converter = channel_converters[src_channels-1][dst_channels-1];). */
    int src_channels = 1, dst_channels = 2;
    Conv channel_converter;
    channel_converter = table2d[src_channels - 1][dst_channels - 1];
    dbgmark_ptr("channel_converter (from 2D array)", (unsigned long long)(size_t)channel_converter);
    dbgmark_ptr("expected (OtherFunc, table2d[0][1])", (unsigned long long)(size_t)OtherFunc);
    if ((size_t)channel_converter != (size_t)OtherFunc) {
        dbgmark("FAIL: local fn-ptr var from 2D array holds wrong/truncated value -- reproduces the bug");
        return 1;
    }
    channel_converter(33, 44);
    dbgmark("PASS: local fn-ptr var from 2D array correct, call succeeded");
    return 0;
}
