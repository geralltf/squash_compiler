/* Isolates a second, separate bug found while root-causing the SDL3 audio
 * hang: real SDL3's SDL_audio_channel_converters.h declares
 *   static const SDL_AudioChannelConverter channel_converters[8][8] = {...};
 * a FILE-SCOPE "static const" 2D array of FUNCTION POINTERS, indexed as
 * channel_converters[src_channels-1][dst_channels-1] and called through
 * directly. Our mono(1ch)->stereo(2ch) repro crashes calling through NULL
 * at exactly this call site once real audio data reaches ConvertAudio() for
 * the first time (previously unreached, because the audio hang -- now
 * fixed -- meant no data was ever queued). */
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

typedef void (*Conv)(float *dst, const float *src, int n);

static void Conv_1_1(float *dst, const float *src, int n) { (void)dst;(void)src;(void)n; dbgmark("Conv_1_1 called"); }
static void Conv_1_2(float *dst, const float *src, int n) { (void)dst;(void)src;(void)n; dbgmark("Conv_1_2 called"); }
static void Conv_2_1(float *dst, const float *src, int n) { (void)dst;(void)src;(void)n; dbgmark("Conv_2_1 called"); }
static void Conv_2_2(float *dst, const float *src, int n) { (void)dst;(void)src;(void)n; dbgmark("Conv_2_2 called"); }

/* Mirrors the real table's shape: 8x8, mostly NULL, a handful of real
 * entries at small indices -- NOT all entries populated, matching real
 * SDL3's own channel_converters table (most [from][to] combos are NULL). */
static const Conv converters[8][8] = {
    { Conv_1_1, Conv_1_2, NULL, NULL, NULL, NULL, NULL, NULL },
    { Conv_2_1, Conv_2_2, NULL, NULL, NULL, NULL, NULL, NULL },
    { NULL },
    { NULL },
    { NULL },
    { NULL },
    { NULL },
    { NULL },
};

int main(void) {
    dbgmark("start");

    dbgmark("--- raw flat memory dump of converters[8][8] (first 4 rows) ---");
    const Conv *flat = (const Conv *)converters;
    int idx;
    for (idx = 0; idx < 32; idx++) {
        dbgmark_ptr("flat[idx]", (unsigned long long)(size_t)flat[idx]);
    }
    dbgmark_ptr("expected Conv_1_1", (unsigned long long)(size_t)Conv_1_1);
    dbgmark_ptr("expected Conv_1_2", (unsigned long long)(size_t)Conv_1_2);
    dbgmark_ptr("expected Conv_2_1", (unsigned long long)(size_t)Conv_2_1);
    dbgmark_ptr("expected Conv_2_2", (unsigned long long)(size_t)Conv_2_2);

    dbgmark("--- via [row][0] and [row][1] indexing ---");
    dbgmark_ptr("converters[0][0]", (unsigned long long)(size_t)converters[0][0]);
    dbgmark_ptr("converters[0][1]", (unsigned long long)(size_t)converters[0][1]);
    dbgmark_ptr("converters[1][0]", (unsigned long long)(size_t)converters[1][0]);
    dbgmark_ptr("converters[1][1]", (unsigned long long)(size_t)converters[1][1]);

    int src_channels = 1, dst_channels = 2;
    dbgmark_ptr("index [src-1][dst-1]", (unsigned long long)((src_channels-1)*8+(dst_channels-1)));

    Conv c = converters[src_channels - 1][dst_channels - 1];
    dbgmark_ptr("converters[0][1] fnptr", (unsigned long long)(size_t)c);
    dbgmark_ptr("expected (Conv_1_2)", (unsigned long long)(size_t)Conv_1_2);

    if (c == NULL) {
        dbgmark("FAIL: got NULL, would crash calling through it -- reproduces the bug");
        return 1;
    }
    if (c != Conv_1_2) {
        dbgmark("FAIL: got a WRONG (non-NULL) function pointer");
        return 1;
    }
    c(NULL, NULL, 0);
    dbgmark("PASS: correct function pointer read and called");
    return 0;
}
