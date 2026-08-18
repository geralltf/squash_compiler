/* Targeted repro for the SDL3 audio-stream hang: real SDL3's SDL_malloc.c
 * (dlmalloc port) guards its allocator with a GLOBAL spin lock built from
 * the real Win32 InterlockedExchange, exactly this shape (see
 * stdlib/SDL_malloc.c's CAS_LOCK/spin_acquire_lock, ~line 1900-1937):
 *
 *   while (*sl != 0 || InterlockedExchange(sl, 1)) { ...yield... }
 *   ...critical section...
 *   InterlockedExchange(sl, 0);
 *
 * The earlier isolated mutex/semaphore repros only ever exercised ONE
 * thread's view of a lock at a time (alternating turns, or a single thread
 * doing sequential/nested cycles) -- never TWO threads genuinely racing the
 * SAME spin lock at the same instant, hundreds of times a second, which is
 * exactly what happens once the audio playback thread starts calling
 * SDL_malloc/SDL_free (buffer management) at the same time the main thread
 * is inside SDL_PutAudioStreamData's own SDL_malloc-backed queue growth.
 * If this hangs, the bug is in squash's codegen for a real, contended,
 * multi-thread InterlockedExchange spin lock -- not the mutex/semaphore
 * primitives already ruled out. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static volatile long g_lock = 0;
static volatile long g_counter = 0;
static volatile long g_should_stop = 0;

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

/* exact CAS_LOCK/spin_acquire_lock/ACQUIRE_LOCK/RELEASE_LOCK shape from
 * real SDL_malloc.c's non-recursive spin-lock branch. */
static int cas_lock(volatile long *sl) {
    return InterlockedExchange(sl, (LONG)1);
}
static void clear_lock(volatile long *sl) {
    InterlockedExchange(sl, (LONG)0);
}
static void spin_acquire_lock(volatile long *sl) {
    int spins = 0;
    while (*sl != 0 || cas_lock(sl)) {
        spins++;
        if ((spins & 63) == 0) {
            SleepEx(1, FALSE);
        }
    }
}
static void acquire_lock(volatile long *sl) {
    if (cas_lock(sl)) spin_acquire_lock(sl);
}

static DWORD WINAPI worker_thread(LPVOID param) {
    int tid = (int)(size_t)param;
    long n = 0;
    while (!g_should_stop) {
        acquire_lock(&g_lock);
        /* small critical section, like malloc/free touching shared state */
        long before = g_counter;
        g_counter = before + 1;
        clear_lock(&g_lock);
        n++;
        if ((n & 8191) == 0) {
            dbgmark_ptr("worker cycles, tid-tag", (unsigned long long)tid);
        }
    }
    dbgmark_ptr("worker done, total cycles", (unsigned long long)n);
    return 0;
}

int main(void) {
    dbgmark("start");

    HANDLE threads[3];
    int i;
    for (i = 0; i < 3; i++) {
        threads[i] = CreateThread(NULL, 0, worker_thread, (LPVOID)(size_t)(i + 1), 0, NULL);
    }
    dbgmark("3 worker threads launched, racing the same spin lock");

    /* also race from the main thread itself */
    long n = 0;
    int iter;
    for (iter = 0; iter < 200000; iter++) {
        acquire_lock(&g_lock);
        long before = g_counter;
        g_counter = before + 1;
        clear_lock(&g_lock);
        n++;
        if ((n % 20000) == 0) {
            dbgmark_ptr("main cycles", (unsigned long long)n);
        }
    }
    dbgmark("main thread finished 200000 cycles -- signalling stop");
    InterlockedExchange(&g_should_stop, 1);

    for (i = 0; i < 3; i++) {
        WaitForSingleObject(threads[i], INFINITE);
        dbgmark_ptr("joined worker", (unsigned long long)i);
    }

    dbgmark_ptr("final g_counter", (unsigned long long)g_counter);
    dbgmark("ALL DONE -- no deadlock");
    return 0;
}
