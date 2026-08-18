/* Dedicated single-TU Linux driver for the "clear" example (examples/
 * renderer/01-clear/clear.c), built by Makefile.SDL3.linux.clear.
 *
 * Uses the same plain from-source path as sdl_unity.c (#include
 * "sdl_core.inc" directly, no ".sqo" cache) rather than the cached-object
 * path build_example.sh/Makefile.SDL3.linux uses for the other 38
 * examples: linking clear.c's own driver object against
 * SDL3_Build/sdl_common.sqo hits a real, separate squash objfile_merge bug
 * where a symbol the .sqo genuinely exports (confirmed present in its
 * export table, e.g. SDL_fabsf) still resolves as an unresolved dynamic
 * import at final link — a cross-object symbol resolution issue in the
 * ".sqo" linking path specifically, not reproduced when everything is
 * compiled as one translation unit like this. Left as a standing issue for
 * whoever wants to make the shared-cache path fully reliable next.
 */
#include "sdl_core.inc"
#include "examples/renderer/01-clear/clear.c"
#include "example_shim.inc"
