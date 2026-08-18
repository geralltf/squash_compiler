/* Targeted repro #2 for the SDL3 audio hang: CreateChunkedAudioTrack reads
 * "queue->chunk_pool.block_size" -- a POINTER, then an EMBEDDED (non-pointer)
 * struct MEMBER, then a FIELD inside that embedded struct. This mirrors real
 * SDL_AudioQueue's exact shape (SDL_audioqueue.c):
 *
 *   struct SDL_MemoryPool { void *free_blocks; size_t block_size;
 *                            size_t num_free; size_t max_free; };
 *   struct SDL_AudioQueue { SDL_AudioTrack *head, *tail;
 *                            Uint8 *history_buffer;
 *                            size_t history_length, history_capacity;
 *                            SDL_MemoryPool track_pool, chunk_pool; };
 *
 * repro_8arg_call.c already ruled out the *call* (stack-argument-passing)
 * side of CreateChunkedAudioTrack -- this repro instead targets the *read*
 * side: does "ptr->embedded_struct_member.field" correctly compute the
 * embedded struct's byte offset within the outer struct, for a struct
 * shaped exactly like SDL_AudioQueue (two pointers, three more pointers/
 * size_ts, THEN two back-to-back embedded structs)? */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

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

typedef struct MyTrack MyTrack;

typedef struct MyMemoryPool {
    void *free_blocks;
    size_t block_size;
    size_t num_free;
    size_t max_free;
} MyMemoryPool;

typedef struct MyAudioQueue {
    MyTrack *head;
    MyTrack *tail;
    unsigned char *history_buffer;
    size_t history_length;
    size_t history_capacity;
    MyMemoryPool track_pool;
    MyMemoryPool chunk_pool;
} MyAudioQueue;

typedef struct MySpec {
    unsigned int format;
    int channels;
    int freq;
} MySpec;

#define MY_FRAMESIZE(x) ((((x).format) & 0xFFu) / 8 * (x).channels)

static void MyInitMemoryPool(MyMemoryPool *pool, size_t block_size, size_t max_free) {
    pool->free_blocks = 0;
    pool->block_size = block_size;
    pool->num_free = 0;
    pool->max_free = max_free;
}

static MyAudioQueue *MyCreateAudioQueue(size_t chunk_size) {
    MyAudioQueue *queue = (MyAudioQueue *)calloc(1, sizeof(*queue));
    MyInitMemoryPool(&queue->track_pool, sizeof(void*)*4, 8);
    MyInitMemoryPool(&queue->chunk_pool, chunk_size, 4);
    return queue;
}

/* mirrors CreateChunkedAudioTrack's exact computation */
static size_t ComputeCapacity(MyAudioQueue *queue, MySpec *spec) {
    size_t capacity = queue->chunk_pool.block_size;
    dbgmark_ptr("  inside: capacity before", (unsigned long long)capacity);
    size_t framesize = MY_FRAMESIZE(*spec);
    dbgmark_ptr("  inside: framesize", (unsigned long long)framesize);
    size_t rem = capacity % framesize;
    dbgmark_ptr("  inside: capacity %% framesize", (unsigned long long)rem);
    capacity -= rem;
    dbgmark_ptr("  inside: capacity after", (unsigned long long)capacity);
    capacity = queue->chunk_pool.block_size;
    capacity -= capacity % MY_FRAMESIZE(*spec);
    dbgmark_ptr("  inside: capacity after ORIGINAL ONE-LINER", (unsigned long long)capacity);
    return capacity;
}

int main(void) {
    dbgmark("start");
    MyAudioQueue *queue = MyCreateAudioQueue(8192);
    dbgmark_ptr("queue->track_pool.block_size", (unsigned long long)queue->track_pool.block_size);
    dbgmark_ptr("queue->chunk_pool.block_size (expect 8192)", (unsigned long long)queue->chunk_pool.block_size);

    MySpec spec;
    spec.format = 0x8120; /* SDL_AUDIO_F32LE */
    spec.channels = 1;
    spec.freq = 8000;

    {
        MySpec *sp = &spec;
        dbgmark_ptr("(*sp).format", (unsigned long long)(*sp).format);
        dbgmark_ptr("(*sp).format & 0xFFu", (unsigned long long)(((*sp).format) & 0xFFu));
        dbgmark_ptr("bitsize / 8", (unsigned long long)((((*sp).format) & 0xFFu) / 8));
        dbgmark_ptr("(*sp).channels", (unsigned long long)(*sp).channels);
        dbgmark_ptr("bytesize * channels", (unsigned long long)(((((*sp).format) & 0xFFu) / 8) * (*sp).channels));
        dbgmark_ptr("macro result direct", (unsigned long long)MY_FRAMESIZE(*sp));
    }

    size_t cap = ComputeCapacity(queue, &spec);
    dbgmark_ptr("computed capacity (expect 8192)", (unsigned long long)cap);

    if (cap == 8192) {
        dbgmark("PASS: nested member access correct");
    } else {
        dbgmark("FAIL: nested member access WRONG -- reproduces the bug");
    }
    return 0;
}
