/* Fourth bug candidate in the audio-hang investigation chain: real SDL3's
 * ConvertAudio() has an 11-PARAMETER signature (int, const void*,
 * SDL_AudioFormat, int, const int*, void*, SDL_AudioFormat, int, const
 * int*, void*, float) -- only the first 4 are register-passed on Win64,
 * the remaining 7 (including the LAST one, a float "gain") are
 * stack-passed. The crash is inside ConvertAudio's "if (gain != 1.0f)"
 * gain-scaling loop even though the real caller computes gain via a
 * ternary that should select 1.0f for our upsampling scenario -- so
 * either the ternary itself or the float stack-argument passing this many
 * parameters deep is suspect. This repro isolates the ARGUMENT-PASSING
 * side: an 11-param function ending in a float, called with a
 * ternary-computed value that should be exactly 1.0f. */
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
static void dbgmark_float(const char *label, float v) {
    union { float f; unsigned int u; } pun;
    pun.f = v;
    dbgmark_ptr(label, (unsigned long long)pun.u);
}

/* mirrors ConvertAudio's exact 11-param shape and stack position of gain */
static void ElevenParamFn(int num_frames, const void *src, int src_format, int src_channels,
                           const int *src_map, void *dst, int dst_format, int dst_channels,
                           const int *dst_map, void *scratch, float gain) {
    (void)src; (void)src_format; (void)src_channels; (void)src_map;
    (void)dst; (void)dst_format; (void)dst_channels; (void)dst_map; (void)scratch;
    dbgmark_ptr("callee: num_frames", (unsigned long long)num_frames);
    dbgmark_float("callee: gain (raw bits)", gain);
    if (gain != 1.0f) {
        dbgmark_float("callee: gain != 1.0f, value", gain);
        dbgmark("callee: WOULD enter gain-scaling loop -- reproduces the bug (if gain should be 1.0)");
    } else {
        dbgmark("callee: gain correctly == 1.0f, loop skipped");
    }
}

int main(void) {
    dbgmark("start");
    int input_frames = 100, output_frames = 551; /* mimics 8000Hz->44100Hz upsample: output > input */
    float real_gain = 1.0f; /* SDL_AudioStream's default gain */
    float postresample_gain = (input_frames > output_frames) ? real_gain : 1.0f;
    dbgmark_float("caller: postresample_gain computed", postresample_gain);

    ElevenParamFn(output_frames, (const void*)0x1111, 0x8120, 1,
                  (const int*)0, (void*)0x2222, 0x8010, 2,
                  (const int*)0, (void*)0x3333, postresample_gain);

    return 0;
}
