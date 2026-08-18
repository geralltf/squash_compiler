#ifndef _OSATOMIC_H
#define _OSATOMIC_H
#include "include/stdint.h"
#include "include/stdbool.h"

/* Deprecated-but-still-exported libSystem entry points that SDL3's
 * SDL_atomic.c calls directly on Apple platforms. Declared here (rather
 * than pulling in Apple's real header, which needs clang attribute
 * machinery squash's preprocessor doesn't implement) with "extern": real
 * libSystem exports with no body anywhere squash ever compiles -- see
 * include/unistd.h's header comment for why plain "extern" matters here
 * instead of a bodyless prototype. */
extern bool    OSAtomicCompareAndSwap32Barrier(int32_t oldValue, int32_t newValue, volatile int32_t *theValue);
extern int32_t OSAtomicOr32Barrier(uint32_t mask, volatile uint32_t *theValue);
extern int64_t OSAtomicAdd64Barrier(int64_t theAmount, volatile int64_t *theValue);

#endif
