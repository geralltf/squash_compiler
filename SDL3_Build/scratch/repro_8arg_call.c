/* Targeted repro for the SDL3 audio hang's confirmed root symptom: a track
 * created by SDL_CreateAudioTrack() always has capacity==0 (verified via a
 * live memory peek of the real, running SDL_AudioTrack struct -- flushed/
 * head/tail/capacity all zero, thousands of tracks chained, an infinite
 * loop). SDL_CreateAudioTrack has EXACTLY this 8-argument Win64 shape:
 *
 *   SDL_AudioTrack *SDL_CreateAudioTrack(
 *       SDL_AudioQueue *queue, const SDL_AudioSpec *spec, const int *chmap,
 *       Uint8 *data, size_t len, size_t capacity,
 *       SDL_ReleaseAudioBufferCallback callback, void *userdata)
 *
 * 4 register args (RCX/RDX/R8/R9) + 4 STACK args (len, capacity, callback,
 * userdata) -- and it's called (CreateChunkedAudioTrack) with a literal 0
 * for len, a local variable for capacity, a bare FUNCTION NAME (address-of,
 * not a call) for callback, and a variable again for userdata -- exactly
 * reproduced here with the same argument *shapes* in the same stack
 * positions, to see whether squash's callee correctly reads back the 6th
 * argument (2nd stack slot, "capacity"). */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef void (*ReleaseCB)(void *userdata, const void *buf, int len);

typedef struct {
    void *a; void *b; void *c; void *d;
    size_t len_seen;
    size_t capacity_seen;
    ReleaseCB cb_seen;
    void *userdata_seen;
} Track;

static Track g_last;

static void dbgmark(const char *s) {
    HANDLE h = GetStdHandle((DWORD)-11);
    DWORD written;
    WriteFile(h, s, (DWORD)strlen(s), &written, NULL);
    WriteFile(h, "\n", 1, &written, NULL);
}

static void dbgmark_ptr(const char *label, unsigned long long v) {
    char buf[64];
    int p = 0;
    const char *lbl = label;
    while (*lbl) buf[p++] = *lbl++;
    buf[p++] = '=';
    buf[p++] = '0';
    buf[p++] = 'x';
    int shift;
    for (shift = 60; shift >= 0; shift -= 4) {
        int nib = (int)((v >> shift) & 0xf);
        buf[p++] = (nib < 10) ? ('0' + nib) : ('a' + (nib - 10));
    }
    buf[p] = 0;
    dbgmark(buf);
}

static void MyReleaseCallback(void *userdata, const void *buf, int len) {
    (void)userdata; (void)buf; (void)len;
}

/* Track *CreateTrack(queue, spec, chmap, data, len, capacity, callback, userdata) */
static Track *CreateTrack(void *queue, void *spec, void *chmap, void *data,
                           size_t len, size_t capacity,
                           ReleaseCB callback, void *userdata) {
    g_last.a = queue; g_last.b = spec; g_last.c = chmap; g_last.d = data;
    g_last.len_seen = len;
    g_last.capacity_seen = capacity;
    g_last.cb_seen = callback;
    g_last.userdata_seen = userdata;
    return &g_last;
}

static Track *CreateChunkedTrack(void *queue, void *spec, void *chmap) {
    /* mirror CreateChunkedAudioTrack: capacity computed into a local first,
     * then a literal 0, the local, a bare function name, and a variable
     * passed as the 5th..8th (stack) arguments, in that order. */
    size_t capacity = 8192;
    capacity -= capacity % 4;
    Track *t = CreateTrack(queue, spec, chmap, (void*)0x1234,
                            0, capacity, MyReleaseCallback, queue);
    return t;
}

int main(void) {
    dbgmark("start");
    void *fake_queue = (void*)0x5555;
    void *fake_spec = (void*)0x6666;
    void *fake_chmap = (void*)0x7777;

    Track *t = CreateChunkedTrack(fake_queue, fake_spec, fake_chmap);

    dbgmark_ptr("t->len_seen", (unsigned long long)t->len_seen);
    dbgmark_ptr("t->capacity_seen", (unsigned long long)t->capacity_seen);
    dbgmark_ptr("t->cb_seen", (unsigned long long)(size_t)t->cb_seen);
    dbgmark_ptr("t->userdata_seen", (unsigned long long)(size_t)t->userdata_seen);
    dbgmark_ptr("expected capacity", 8192);
    dbgmark_ptr("expected cb (MyReleaseCallback)", (unsigned long long)(size_t)MyReleaseCallback);
    dbgmark_ptr("expected userdata (fake_queue)", (unsigned long long)(size_t)fake_queue);

    if (t->capacity_seen == 8192) {
        dbgmark("PASS: capacity correct");
    } else {
        dbgmark("FAIL: capacity WRONG -- reproduces the bug");
    }
    return 0;
}
