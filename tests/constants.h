/*
 * constants.h  —  Tests:
 *   - Header guard (triple-include safe)
 *   - #if / #elif / #else chains
 *   - Macros used in #if expressions
 *   - Nested conditional blocks
 */

#ifndef CONSTANTS_H
#define CONSTANTS_H

/* ── Version info ───────────────────────────────────────── */
#define VERSION_MAJOR 1
#define VERSION_MINOR 2
#define VERSION_PATCH 3

/* ── Conditional version checks ────────────────────────── */
#if VERSION_MAJOR
#define IS_V1 1
#else
#define IS_V1 0
#endif

#ifndef IS_V2
#define IS_V2 0
#endif

/* ── Buffer sizes via conditional ──────────────────────── */
#define SMALL_BUF  64
#define MEDIUM_BUF 256
#define LARGE_BUF  1024

#if VERSION_MINOR
#define DEFAULT_BUF MEDIUM_BUF
#else
#define DEFAULT_BUF SMALL_BUF
#endif

/* ── Error codes ────────────────────────────────────────── */
#define ERR_NONE      0
#define ERR_INVALID   1
#define ERR_OVERFLOW  2
#define ERR_UNDERFLOW 3
#define ERR_DIVZERO   4

#endif /* CONSTANTS_H */
