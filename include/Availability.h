#ifndef _AVAILABILITY_H
#define _AVAILABILITY_H
/* Minimal shim: SDL_internal.h only checks "#ifndef __MAC_OS_X_VERSION_MAX_ALLOWED"
 * and falls back to 0 itself when unset, so this file only needs to exist
 * (real Apple headers pull in clang attribute machinery squash's
 * preprocessor doesn't implement). */
#endif
