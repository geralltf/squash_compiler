/* Targeted repro for the SDL3 audio-stream hang: tests RECURSIVE (nested,
 * same-thread) locking of a mutex built exactly like real SDL3's
 * thread/generic/SDL_sysmutex.c (semaphore-backed, "owner == this_thread"
 * check for the recursive fast-path) paired with a real Win32 kernel
 * semaphore, matching this build's own sdl_core.inc pairing. The prior
 * isolated repro that passed 300+ cycles alternated lock/unlock between TWO
 * threads -- it never tested ONE thread locking the SAME mutex twice before
 * unlocking, which is exactly the pattern real SDL3 code uses in a few
 * places (e.g. "lock to check X, unlock, lock again" is fine, but a couple
 * of call sites nest an inner Lock/Unlock pair inside an outer held lock).
 * If squash's codegen misreads the Uint64 "owner" field back from the heap
 * struct (e.g. a struct padding/offset bug for {int; Uint64; pointer}),
 * "owner == this_thread" is always false, so a nested lock takes the
 * WaitSemaphore path on an already-fully-consumed semaphore -- permanent
 * self-deadlock, matching the real hang's symptoms (thread parked in a real
 * kernel wait, hangs on the very first reentrant lock it ever attempts). */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef uint64_t MyThreadID;

typedef struct MySem {
    HANDLE h;
} MySem;

typedef struct MyMutex {
    int recursive;
    MyThreadID owner;
    MySem *sem;
} MyMutex;

static MySem *MyCreateSemaphore(uint32_t initial_value) {
    MySem *sem = (MySem *)malloc(sizeof(MySem));
    sem->h = CreateSemaphoreW(NULL, (LONG)initial_value, 0x7fffffff, NULL);
    return sem;
}

static void MyWaitSemaphore(MySem *sem) {
    WaitForSingleObject(sem->h, INFINITE);
}

static void MySignalSemaphore(MySem *sem) {
    ReleaseSemaphore(sem->h, 1, NULL);
}

static MyThreadID MyGetCurrentThreadID(void) {
    return (MyThreadID)GetCurrentThreadId();
}

static MyMutex *MyCreateMutex(void) {
    MyMutex *mutex = (MyMutex *)calloc(1, sizeof(MyMutex));
    mutex->sem = MyCreateSemaphore(1);
    mutex->recursive = 0;
    mutex->owner = 0;
    return mutex;
}

static void MyLockMutex(MyMutex *mutex) {
    MyThreadID this_thread = MyGetCurrentThreadID();
    if (mutex->owner == this_thread) {
        ++mutex->recursive;
    } else {
        MyWaitSemaphore(mutex->sem);
        mutex->owner = this_thread;
        mutex->recursive = 0;
    }
}

static void MyUnlockMutex(MyMutex *mutex) {
    if (mutex->recursive) {
        --mutex->recursive;
    } else {
        mutex->owner = 0;
        MySignalSemaphore(mutex->sem);
    }
}

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

int main(void) {
    dbgmark("start");
    MyMutex *m = MyCreateMutex();
    dbgmark("mutex created");

    MyLockMutex(m);
    dbgmark("outer lock acquired");

    /* the critical test: lock the SAME mutex again from the SAME thread
     * before unlocking -- this must take the fast "recursive" path, not
     * block. */
    MyLockMutex(m);
    dbgmark("inner (recursive) lock acquired -- PASS, no hang");

    MyUnlockMutex(m);
    dbgmark("inner unlock done");

    MyUnlockMutex(m);
    dbgmark("outer unlock done -- full pass");

    /* now the OTHER pattern: sequential, non-nested lock/unlock cycles from
     * this same single thread -- exactly what SDL_GetAudioStreamQueued then
     * SDL_PutAudioStreamData do back-to-back on stream->lock. */
    int i;
    for (i = 0; i < 5; i++) {
        dbgmark_ptr("sequential lock, i", (unsigned long long)i);
        MyLockMutex(m);
        dbgmark_ptr("sequential lock acquired, i", (unsigned long long)i);
        MyUnlockMutex(m);
        dbgmark_ptr("sequential unlock done, i", (unsigned long long)i);
    }
    dbgmark("all sequential cycles done -- full pass");

    return 0;
}
