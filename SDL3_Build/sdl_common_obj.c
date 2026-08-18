/* Compiled once with "squash -c" to squash_build/sdl_common.sqo, then linked
 * against by small "driver" files (sdl_unity.c, examples_gen/*.c) instead of
 * recompiling the whole ~2MB shared SDL3 body from source every time — see
 * objfile.h for how squash's object-file support works. This file exists
 * purely to give that shared body a name; it defines no main() of its own. */
#include "sdl_core.inc"
