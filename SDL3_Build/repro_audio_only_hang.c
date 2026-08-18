/* Isolates the simple-playback.exe hang: does it need the video subsystem
 * (window/renderer/PeekMessage loop) at all, or does a pure SDL_INIT_AUDIO
 * program with no window hang the same way? Real SDL3 public API only. */
#include "sdl_core.inc"

int main(void)
{
    squash_init_private_bootstrap();
    dbgmark("audio-only repro: before SDL_Init(AUDIO)");
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        dbgmark("SDL_Init failed");
        return 1;
    }
    dbgmark("audio-only repro: after SDL_Init(AUDIO)");

    SDL_AudioSpec spec;
    spec.channels = 1;
    spec.format = SDL_AUDIO_F32;
    spec.freq = 8000;
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!stream) {
        dbgmark("SDL_OpenAudioDeviceStream failed");
        return 1;
    }
    dbgmark("audio-only repro: stream opened");
    SDL_ResumeAudioStreamDevice(stream);
    dbgmark("audio-only repro: resumed, entering loop");

    int current_sine_sample = 0;
    int iter;
    for (iter = 0; iter < 500; iter++) {
        const int minimum_audio = (8000 * (int)sizeof(float)) / 2;
        if (SDL_GetAudioStreamQueued(stream) < minimum_audio) {
            static float samples[512];
            int i;
            for (i = 0; i < 512; i++) {
                const int freq = 440;
                const float phase = current_sine_sample * freq / 8000.0f;
                samples[i] = SDL_sinf(phase * 2 * SDL_PI_F);
                current_sine_sample++;
            }
            current_sine_sample %= 8000;
            SDL_PutAudioStreamData(stream, samples, sizeof(samples));
        }
        if (iter % 50 == 0) {
            dbgmark_ptr("audio-only repro: iter", (unsigned long long)iter);
        }
        SDL_Delay(5);
    }
    dbgmark("audio-only repro: loop finished OK");
    SDL_Quit();
    return 0;
}
