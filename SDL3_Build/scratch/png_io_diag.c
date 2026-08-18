#include "../sdl_core.inc"

int main(void)
{
    if (!SDL_Init(0)) { SDL_Log("init fail"); return 1; }
    const char *path = "squash_test_image.png";
    SDL_IOStream *s = SDL_IOFromFile(path, "rb");
    if (!s) { SDL_Log("IOFromFile failed: %s", SDL_GetError()); return 1; }
    dbgmark("IOFromFile OK");
    Uint8 magic[8];
    size_t n = SDL_ReadIO(s, magic, 8);
    dbgmark_ptr("bytes read", (unsigned long long)n);
    int i;
    for (i = 0; i < 8; i++) dbgmark_ptr("byte", (unsigned long long)magic[i]);
    SDL_SeekIO(s, 0, SDL_IO_SEEK_SET);
    bool ispng = SDL_IsPNG(s);
    dbgmark_ptr("SDL_IsPNG", (unsigned long long)ispng);
    SDL_CloseIO(s);
    return 0;
}
