#include <windows.h>
#include <stdio.h>
#include <math.h>

/* Adds back in exactly the one piece test_winmm_raw_no_thread.c deliberately
 * left out: a SEPARATE thread owns and drives the WinMM device (waveOutWrite
 * happens off the main thread), while main() produces sine samples into a
 * mutex-protected queue -- matching the real shape of SDL3's internal
 * SDL_AudioTrack/SDL_AudioQueue + audio-device-thread architecture. Compiled
 * against squash's OWN include/windows.h stub (not the real Microsoft SDK
 * headers used earlier this session), from the squash repo root, so this
 * exercises exactly the header surface + codegen path the real SDL3_Build
 * examples use. If this hangs, the bug is in the mutex/thread <-> WinMM
 * interaction; if it doesn't, the search narrows further to SDL3's actual
 * internal audio subsystem code (SDL_audio.c / SDL_RunAudio), not squash. */

typedef struct MyMutex {
    HANDLE sem;
} MyMutex;
static MyMutex *MyCreateMutex(void) {
    MyMutex *m = (MyMutex*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MyMutex));
    m->sem = CreateSemaphoreW(NULL, 1, 1, NULL);
    return m;
}
static void MyLockMutex(MyMutex *m)   { WaitForSingleObject(m->sem, INFINITE); }
static void MyUnlockMutex(MyMutex *m) { ReleaseSemaphore(m->sem, 1, NULL); }

#define SAMPLE_RATE 8000
#define SAMPLES_PER_BUFFER 2048
#define N_QUEUE_SLOTS 8
#define TOTAL_SECONDS 2

typedef struct AudioBlock {
    short data[SAMPLES_PER_BUFFER];
    int len_samples;
    int in_use; /* currently owned by the device (submitted to WinMM) */
} AudioBlock;

static AudioBlock g_blocks[N_QUEUE_SLOTS];
static WAVEHDR g_hdrs[N_QUEUE_SLOTS];
static int g_queue_head = 0; /* next slot main() will fill */
static int g_queue_count = 0; /* how many filled slots are waiting for the device thread */
static MyMutex *g_lock;
static volatile int g_running = 1;
static volatile long g_buffers_submitted = 0;
static volatile long g_buffers_completed = 0;
static HWAVEOUT g_hwo;

static DWORD WINAPI device_thread(LPVOID param) {
    (void)param;
    int read_idx = 0;
    int outstanding[N_QUEUE_SLOTS];
    for (int i = 0; i < N_QUEUE_SLOTS; i++) outstanding[i] = 0;

    while (g_running || g_queue_count > 0) {
        MyLockMutex(g_lock);

        /* Reap any completed headers so their slots can be reused. */
        for (int i = 0; i < N_QUEUE_SLOTS; i++) {
            if (outstanding[i] && (g_hdrs[i].dwFlags & WHDR_DONE)) {
                waveOutUnprepareHeader(g_hwo, &g_hdrs[i], sizeof(WAVEHDR));
                outstanding[i] = 0;
                g_blocks[i].in_use = 0;
                g_buffers_completed++;
            }
        }

        /* Submit the next queued block, if any. */
        if (g_queue_count > 0) {
            int idx = read_idx;
            g_hdrs[idx].lpData = (LPSTR)g_blocks[idx].data;
            g_hdrs[idx].dwBufferLength = g_blocks[idx].len_samples * sizeof(short);
            g_hdrs[idx].dwFlags = 0;
            g_hdrs[idx].dwLoops = 0;
            waveOutPrepareHeader(g_hwo, &g_hdrs[idx], sizeof(WAVEHDR));
            waveOutWrite(g_hwo, &g_hdrs[idx], sizeof(WAVEHDR));
            outstanding[idx] = 1;
            g_buffers_submitted++;
            read_idx = (read_idx + 1) % N_QUEUE_SLOTS;
            g_queue_count--;
        }

        MyUnlockMutex(g_lock);
        Sleep(5);
    }

    /* Drain: wait for all outstanding headers to finish. */
    int pending = 1;
    int guard = 0;
    while (pending && guard < 2000) {
        pending = 0;
        MyLockMutex(g_lock);
        for (int i = 0; i < N_QUEUE_SLOTS; i++) {
            if (outstanding[i]) {
                if (g_hdrs[i].dwFlags & WHDR_DONE) {
                    waveOutUnprepareHeader(g_hwo, &g_hdrs[i], sizeof(WAVEHDR));
                    outstanding[i] = 0;
                    g_buffers_completed++;
                } else {
                    pending = 1;
                }
            }
        }
        MyUnlockMutex(g_lock);
        if (pending) Sleep(5);
        guard++;
    }

    printf("device_thread: exiting, submitted=%ld completed=%ld\n", g_buffers_submitted, g_buffers_completed);
    return 0;
}

static void fill_sine(short *buf, int n, int *phase_sample) {
    const int freq = 440;
    for (int i = 0; i < n; i++) {
        double phase = (double)(*phase_sample) * freq / SAMPLE_RATE;
        buf[i] = (short)(sin(phase * 2.0 * 3.14159265358979) * 16000.0);
        (*phase_sample)++;
    }
    *phase_sample %= SAMPLE_RATE;
}

int main(void) {
    WAVEFORMATEX wfx;
    MMRESULT mr;
    int phase = 0;
    int write_idx = 0;

    printf("main: start\n");
    g_lock = MyCreateMutex();

    wfx.wFormatTag = 1;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    mr = waveOutOpen(&g_hwo, 0xFFFFFFFF, &wfx, 0, 0, CALLBACK_NULL);
    if (mr != 0) {
        printf("waveOutOpen failed: %d\n", (int)mr);
        return 1;
    }
    printf("main: waveOutOpen OK\n");

    HANDLE dev = CreateThread(NULL, 0, device_thread, NULL, 0, NULL);
    if (!dev) {
        printf("main: CreateThread failed\n");
        return 1;
    }
    printf("main: device thread created\n");

    int total_blocks_to_produce = (SAMPLE_RATE * TOTAL_SECONDS) / SAMPLES_PER_BUFFER;
    int produced = 0;
    int main_iterations = 0;
    int max_main_iterations = 20000;

    while (produced < total_blocks_to_produce && main_iterations < max_main_iterations) {
        MyLockMutex(g_lock);
        int can_produce = (g_queue_count < N_QUEUE_SLOTS) && !g_blocks[write_idx].in_use;
        MyUnlockMutex(g_lock);

        if (can_produce) {
            fill_sine(g_blocks[write_idx].data, SAMPLES_PER_BUFFER, &phase);
            g_blocks[write_idx].len_samples = SAMPLES_PER_BUFFER;

            MyLockMutex(g_lock);
            g_blocks[write_idx].in_use = 1;
            g_queue_count++;
            MyUnlockMutex(g_lock);

            write_idx = (write_idx + 1) % N_QUEUE_SLOTS;
            produced++;
            if (produced % 4 == 0) printf("main: produced=%d\n", produced);
        } else {
            Sleep(5);
        }
        main_iterations++;
    }

    printf("main: production done, produced=%d iterations=%d\n", produced, main_iterations);

    g_running = 0;
    DWORD wr = WaitForSingleObject(dev, 10000);
    printf("main: device thread wait result=%lu\n", (unsigned long)wr);
    CloseHandle(dev);

    waveOutReset(g_hwo);
    waveOutClose(g_hwo);

    printf("main: done, exiting cleanly\n");
    return 0;
}
