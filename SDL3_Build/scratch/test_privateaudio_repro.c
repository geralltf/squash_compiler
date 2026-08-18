#include <windows.h>
#include <stdio.h>

/* Byte-for-byte functional port of SDL3_Build/sdl_part_audio.c's real
 * PRIVATEAUDIO_* backend (struct shape, WaitDevice/PlayDevice/OpenDevice
 * logic unchanged) -- NOT SDL3 source, this project's own custom backend --
 * driven by a plain single-threaded loop instead of SDL3's real audio
 * thread. Goal: reproduce, in total isolation, whether squash's codegen for
 * THIS EXACT struct/array/field-access shape ever fails to observe the
 * WHDR_DONE flag actually being set by the OS after waveOutWrite completes.
 * A hard iteration cap is built into the wait itself so a real hang reports
 * as "STUCK" instead of actually hanging -- this file must never be able to
 * loop forever, regardless of what it finds. */

typedef unsigned char Uint8;

struct SDL_PrivateAudioData {
    HWAVEOUT hwo;
    WAVEHDR  hdrs[2];
    Uint8   *bufs[2];
    int      cur;
};

typedef struct FakeAudioDevice {
    struct SDL_PrivateAudioData *hidden;
} FakeAudioDevice;

static int g_wait_spin_counter = 0;
#define WAIT_SPIN_CAP 200000 /* generous; real waveOut completion is well under this */

/* Exact copy of PRIVATEAUDIO_WaitDevice's logic, plus an escape hatch so
 * THIS test can never truly hang -- the real backend has no such cap, which
 * is the bug under investigation. */
static int PRIVATEAUDIO_WaitDevice_repro(FakeAudioDevice *device)
{
    WAVEHDR *h = &device->hidden->hdrs[device->hidden->cur];
    int local_spins = 0;
    while (!(h->dwFlags & WHDR_DONE)) {
        Sleep(1);
        local_spins++;
        g_wait_spin_counter++;
        if (local_spins > WAIT_SPIN_CAP) {
            printf("PRIVATEAUDIO_WaitDevice_repro: STUCK after %d spins on cur=%d, dwFlags=0x%08lx\n",
                   local_spins, device->hidden->cur, (unsigned long)h->dwFlags);
            return 0; /* would-be-infinite in the real backend; we bail instead */
        }
    }
    return 1;
}

static Uint8 *PRIVATEAUDIO_GetDeviceBuf_repro(FakeAudioDevice *device) {
    return device->hidden->bufs[device->hidden->cur];
}

static void PRIVATEAUDIO_PlayDevice_repro(FakeAudioDevice *device, int buflen)
{
    WAVEHDR *h = &device->hidden->hdrs[device->hidden->cur];
    h->dwBufferLength = (DWORD)buflen;
    h->dwFlags &= ~(DWORD)WHDR_DONE;
    waveOutWrite(device->hidden->hwo, h, (UINT)sizeof(WAVEHDR));
    device->hidden->cur = (device->hidden->cur + 1) % 2;
}

static int PRIVATEAUDIO_OpenDevice_repro(FakeAudioDevice *device, int bufsize)
{
    device->hidden = (struct SDL_PrivateAudioData *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(struct SDL_PrivateAudioData));
    if (!device->hidden) return 0;

    WAVEFORMATEX wfx;
    wfx.wFormatTag = 1; /* WAVE_FORMAT_PCM */
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = 44100;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 4;
    wfx.nAvgBytesPerSec = 44100u * 4u;
    wfx.cbSize = 0;

    if (waveOutOpen(&device->hidden->hwo, 0xFFFFFFFF, &wfx, 0, 0, CALLBACK_NULL) != 0) {
        printf("waveOutOpen failed\n");
        return 0;
    }
    printf("OpenDevice_repro: waveOutOpen OK\n");

    for (int i = 0; i < 2; i++) {
        device->hidden->bufs[i] = (Uint8 *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bufsize);
        if (!device->hidden->bufs[i]) return 0;
        device->hidden->hdrs[i].lpData = (char *)device->hidden->bufs[i];
        device->hidden->hdrs[i].dwBufferLength = (DWORD)bufsize;
        waveOutPrepareHeader(device->hidden->hwo, &device->hidden->hdrs[i], (UINT)sizeof(WAVEHDR));
        device->hidden->hdrs[i].dwFlags |= WHDR_DONE;
    }
    return 1;
}

static void PRIVATEAUDIO_CloseDevice_repro(FakeAudioDevice *device)
{
    if (!device->hidden) return;
    if (device->hidden->hwo) {
        waveOutReset(device->hidden->hwo);
        for (int i = 0; i < 2; i++) {
            waveOutUnprepareHeader(device->hidden->hwo, &device->hidden->hdrs[i], (UINT)sizeof(WAVEHDR));
            HeapFree(GetProcessHeap(), 0, device->hidden->bufs[i]);
        }
        waveOutClose(device->hidden->hwo);
    }
    HeapFree(GetProcessHeap(), 0, device->hidden);
    device->hidden = NULL;
}

int main(void) {
    FakeAudioDevice device;
    int framesize = 2 * sizeof(short);
    int sample_frames = 4096; /* similar order of magnitude to SDL_GetDefaultSampleFramesFromFreq(44100) */
    int bufsize = sample_frames * framesize;

    printf("main: start, bufsize=%d\n", bufsize);

    if (!PRIVATEAUDIO_OpenDevice_repro(&device, bufsize)) {
        printf("main: OpenDevice failed\n");
        return 1;
    }

    int total_iterations = 60; /* ~ a few seconds of audio at this buffer size */
    int stuck = 0;
    for (int i = 0; i < total_iterations; i++) {
        if (!PRIVATEAUDIO_WaitDevice_repro(&device)) {
            stuck = 1;
            printf("main: WaitDevice reported STUCK at iteration %d -- this is the bug, reproduced safely\n", i);
            break;
        }
        Uint8 *buf = PRIVATEAUDIO_GetDeviceBuf_repro(&device);
        /* Fill with silence -- content doesn't matter for this repro. */
        for (int b = 0; b < bufsize; b++) buf[b] = 0;
        PRIVATEAUDIO_PlayDevice_repro(&device, bufsize);
        if (i % 10 == 0) printf("main: iteration=%d cur=%d total_spins_so_far=%d\n", i, device.hidden->cur, g_wait_spin_counter);
    }

    if (!stuck) {
        printf("main: completed all %d iterations with no stuck WaitDevice, total_spins=%d\n", total_iterations, g_wait_spin_counter);
    }

    PRIVATEAUDIO_CloseDevice_repro(&device);
    printf("main: done, exiting cleanly (stuck=%d)\n", stuck);
    return stuck;
}
