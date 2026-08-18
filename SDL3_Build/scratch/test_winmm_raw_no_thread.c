#include <windows.h>
#include <stdio.h>
#include <math.h>

/* Raw WinMM sine-wave playback, deliberately NOT going through SDL3 at all:
 * no window, no SDL_CreateThread/_beginthreadex, no mutex, no semaphore --
 * everything (including buffer refill) happens synchronously on main().
 * waveOutOpen uses CALLBACK_NULL, so Windows never calls back into this
 * process on a separate thread either; WHDR_DONE is polled by hand.
 *
 * Purpose: isolate whether the real hang seen in simple-playback.exe lives
 * in squash's codegen for the WinMM waveOut* surface itself, or only shows
 * up once SDL3's own internal audio device thread / mutex-protected queue
 * is layered on top of it. If this hangs too, the bug is in WinMM codegen.
 * If this exits cleanly, the bug is specific to the threaded path. */

#define SAMPLE_RATE 8000
#define N_BUFFERS 2
#define SAMPLES_PER_BUFFER 2048
#define TOTAL_SECONDS 2

static short g_buf[N_BUFFERS][SAMPLES_PER_BUFFER];
static WAVEHDR g_hdr[N_BUFFERS];

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
    HWAVEOUT hwo = NULL;
    MMRESULT mr;
    int phase = 0;
    int total_buffers_to_play = (SAMPLE_RATE * TOTAL_SECONDS) / SAMPLES_PER_BUFFER;
    int buffers_played = 0;

    printf("main: start\n");

    wfx.wFormatTag = 1; /* WAVE_FORMAT_PCM */
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    mr = waveOutOpen(&hwo, 0xFFFFFFFF /* WAVE_MAPPER */, &wfx, 0, 0, CALLBACK_NULL);
    if (mr != 0) {
        printf("waveOutOpen failed: %d\n", (int)mr);
        return 1;
    }
    printf("main: waveOutOpen OK, hwo=%p\n", (void*)hwo);

    for (int i = 0; i < N_BUFFERS; i++) {
        fill_sine(g_buf[i], SAMPLES_PER_BUFFER, &phase);
        g_hdr[i].lpData = (LPSTR)g_buf[i];
        g_hdr[i].dwBufferLength = SAMPLES_PER_BUFFER * sizeof(short);
        g_hdr[i].dwFlags = 0;
        g_hdr[i].dwLoops = 0;
        waveOutPrepareHeader(hwo, &g_hdr[i], sizeof(WAVEHDR));
        waveOutWrite(hwo, &g_hdr[i], sizeof(WAVEHDR));
        buffers_played++;
        printf("main: queued initial buffer %d\n", i);
    }

    int next_buf = 0;
    int iterations = 0;
    int max_iterations = 20000; /* hard safety cap, in case WHDR_DONE never sets */
    while (buffers_played < total_buffers_to_play && iterations < max_iterations) {
        if (g_hdr[next_buf].dwFlags & WHDR_DONE) {
            waveOutUnprepareHeader(hwo, &g_hdr[next_buf], sizeof(WAVEHDR));
            fill_sine(g_buf[next_buf], SAMPLES_PER_BUFFER, &phase);
            g_hdr[next_buf].dwFlags = 0;
            waveOutPrepareHeader(hwo, &g_hdr[next_buf], sizeof(WAVEHDR));
            waveOutWrite(hwo, &g_hdr[next_buf], sizeof(WAVEHDR));
            buffers_played++;
            if (buffers_played % 4 == 0) printf("main: buffers_played=%d\n", buffers_played);
            next_buf = (next_buf + 1) % N_BUFFERS;
        } else {
            Sleep(5);
        }
        iterations++;
    }

    printf("main: playback loop finished, buffers_played=%d iterations=%d\n", buffers_played, iterations);

    /* Let the last couple of buffers actually finish playing before reset/close. */
    Sleep(300);
    waveOutReset(hwo);
    for (int i = 0; i < N_BUFFERS; i++) {
        waveOutUnprepareHeader(hwo, &g_hdr[i], sizeof(WAVEHDR));
    }
    waveOutClose(hwo);

    printf("main: done, exiting cleanly\n");
    return 0;
}
