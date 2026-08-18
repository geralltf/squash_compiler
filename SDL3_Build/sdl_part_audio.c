/* Part 2/3 of the split SDL3 build (see sdl_shared_defs.inc's own comment):
 * the audio subsystem, including the custom PRIVATEAUDIO_* WinMM waveOut
 * backend. Compiled once via "squash -c" to sdl_part_audio.sqo. GENERATED
 * by splitting sdl_core.inc verbatim -- see sdl_part_core.c's own comment. */
#include "sdl_shared_defs.inc"

/* HWAVEOUT/WAVEHDR/waveOut* come from squash's own include/windows.h shim.
 * In the original single-TU sdl_core.inc build this was always already
 * pulled in transitively (something in the "core" section -- e.g.
 * thread/windows/SDL_systhread.c or filesystem/windows/SDL_sysfsops.c --
 * includes it first, and its own #ifndef _WINDOWS_H guard makes that a
 * one-time, unity-build-wide effect). Split into a separate TU, this part
 * needs its own explicit include instead of relying on another part having
 * already pulled it in first. */
#include <windows.h>

/* ============================================================================
 * Custom SDL_AUDIO_DRIVER_PRIVATE backend: a real (not dummy) minimal Win32
 * audio-output implementation, hand-written directly in this TU rather than
 * including SDL3's own audio/directsound or audio/wasapi backends (both are
 * COM-based, pulling in a lot of interface/vtable-call machinery well beyond
 * this build's scope). Uses the plain WinMM waveOut API instead — two
 * double-buffered PCM16 buffers, polled (not callback-driven) exactly like
 * the video backend's PeekMessageA polling loop. Always opens the device as
 * 44.1kHz/stereo/S16 regardless of what the app asked for; SDL_AudioStream
 * (SDL_audiocvt.c/SDL_audioresample.c/SDL_audiotypecvt.c, included below)
 * handles converting from the app's requested format — this is the same
 * "the backend may change any of these values" contract real SDL3 backends
 * use (see SDL_audio.c's own OpenPhysicalAudioDevice() comment). */
#include "audio/SDL_sysaudio.h"

struct SDL_PrivateAudioData {
    HWAVEOUT hwo;
    WAVEHDR  hdrs[2];
    Uint8   *bufs[2];
    int      cur;
};

static bool PRIVATEAUDIO_WaitDevice(SDL_AudioDevice *device)
{
    WAVEHDR *h = &device->hidden->hdrs[device->hidden->cur];
    int spins = 0;
    /* See sdl_core.inc's identical fix for the full explanation: this loop
     * previously had no exit condition, and since SDL3's own shutdown path
     * waits on this exact thread with an INFINITE timeout before ever
     * calling CloseDevice() (the only thing that could force-complete
     * pending buffers), an unbounded wait here made the whole process
     * permanently unkillable. */
    while (!(h->dwFlags & WHDR_DONE)) {
        Sleep(1);
        spins++;
        if (spins > 20000) {
            fprintf(stderr, "PRIVATEAUDIO_WaitDevice: stuck waiting on cur=%d dwFlags=0x%08lx after %d spins -- treating device as disconnected\n",
                    device->hidden->cur, (unsigned long)h->dwFlags, spins);
            return false;
        }
    }
    return true;
}

static Uint8 *PRIVATEAUDIO_GetDeviceBuf(SDL_AudioDevice *device, int *buffer_size)
{
    (void)buffer_size;
    return device->hidden->bufs[device->hidden->cur];
}

static bool PRIVATEAUDIO_PlayDevice(SDL_AudioDevice *device, const Uint8 *buffer, int buflen)
{
    (void)buffer;
    WAVEHDR *h = &device->hidden->hdrs[device->hidden->cur];
    h->dwBufferLength = (DWORD)buflen;
    h->dwFlags &= ~(DWORD)WHDR_DONE;
    waveOutWrite(device->hidden->hwo, h, (UINT)sizeof(WAVEHDR));
    device->hidden->cur = (device->hidden->cur + 1) % 2;
    return true;
}

static bool PRIVATEAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    if (device->recording) {
        return SDL_Unsupported();
    }

    /* Force a known-good PCM format regardless of what was requested —
     * SDL_AudioStream converts on our behalf. */
    device->spec.format = SDL_AUDIO_S16;
    device->spec.channels = 2;
    device->spec.freq = 44100;
    device->sample_frames = SDL_GetDefaultSampleFramesFromFreq(device->spec.freq);

    device->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*device->hidden));
    if (!device->hidden) {
        return false;
    }

    int framesize = 2 * (int)sizeof(Sint16);
    int bufsize = device->sample_frames * framesize;

    WAVEFORMATEX wfx;
    SDL_zero(wfx);
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = 44100;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)framesize;
    wfx.nAvgBytesPerSec = 44100u * (DWORD)framesize;

    if (waveOutOpen(&device->hidden->hwo, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        return SDL_SetError("waveOutOpen failed");
    }

    int i;
    for (i = 0; i < 2; i++) {
        device->hidden->bufs[i] = (Uint8 *)SDL_calloc(1, bufsize);
        if (!device->hidden->bufs[i]) {
            return false;
        }
        device->hidden->hdrs[i].lpData = (char *)device->hidden->bufs[i];
        device->hidden->hdrs[i].dwBufferLength = (DWORD)bufsize;
        waveOutPrepareHeader(device->hidden->hwo, &device->hidden->hdrs[i], (UINT)sizeof(WAVEHDR));
        device->hidden->hdrs[i].dwFlags |= WHDR_DONE;
    }

    return true;
}

static void PRIVATEAUDIO_CloseDevice(SDL_AudioDevice *device)
{
    if (!device->hidden) {
        return;
    }
    if (device->hidden->hwo) {
        int i;
        waveOutReset(device->hidden->hwo);
        for (i = 0; i < 2; i++) {
            waveOutUnprepareHeader(device->hidden->hwo, &device->hidden->hdrs[i], (UINT)sizeof(WAVEHDR));
            SDL_free(device->hidden->bufs[i]);
        }
        waveOutClose(device->hidden->hwo);
    }
    SDL_free(device->hidden);
    device->hidden = NULL;
}

static bool PRIVATEAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = PRIVATEAUDIO_OpenDevice;
    impl->CloseDevice = PRIVATEAUDIO_CloseDevice;
    impl->WaitDevice = PRIVATEAUDIO_WaitDevice;
    impl->PlayDevice = PRIVATEAUDIO_PlayDevice;
    impl->GetDeviceBuf = PRIVATEAUDIO_GetDeviceBuf;
    impl->OnlyHasDefaultPlaybackDevice = true;
    impl->OnlyHasDefaultRecordingDevice = true;
    impl->HasRecordingSupport = false;
    return true;
}

/* NOT brace-initialized — see squash_init_private_bootstrap()'s own comment
 * (video's PRIVATE_bootstrap, right below): a global struct-with-function-
 * pointer-field brace initializer at file scope was observed to read back as
 * garbage after Pass 0.5's rewrite in an earlier investigation; assigning
 * the fields as ordinary runtime statements sidesteps it entirely. */
AudioBootStrap PRIVATEAUDIO_bootstrap;

/* See camera's own identical comment a few lines above: squash's single flat
 * symbol table has no per-file "static" scoping, so audio/SDL_audio.c's own
 * "static const AudioBootStrap *const bootstrap[]" would otherwise collide
 * with video/SDL_video.c's and camera/SDL_camera.c's same-named arrays. */
#define bootstrap audio_driver_bootstrap
#include "audio/SDL_audio.c"
#include "audio/SDL_audiocvt.c"
#include "audio/SDL_audioqueue.c"
#include "audio/SDL_audioresample.c"
#include "audio/SDL_audiotypecvt.c"
#include "audio/SDL_mixer.c"
#include "audio/SDL_wave.c"
#undef bootstrap
