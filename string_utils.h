/*
 * string_utils.h  —  Test header demonstrating:
 *   - Header guard (prevents double-inclusion even when included twice)
 *   - #define constants
 *   - enum
 *   - Forward function declarations
 *   - #if / #elif / #else / #endif conditional blocks
 */

#ifndef STRING_UTILS_H
#define STRING_UTILS_H

/* ── Constants ──────────────────────────────────────────── */
#define STR_MAX_LEN   256
#define STR_EMPTY_VAL 0
#define NEWLINE_CHAR  10
#define SPACE_CHAR    32

/* ── Conditional: platform width tag ───────────────────── */
#define PLATFORM_32 0
#define PLATFORM_64 1

#if PLATFORM_64
#define PTR_SIZE 8
#else
#define PTR_SIZE 4
#endif

/* ── enum ───────────────────────────────────────────────── */
enum StrCmpResult {
    STR_LESS    = -1,
    STR_EQUAL   =  0,
    STR_GREATER =  1
};

/* ── Forward declarations ───────────────────────────────── */
int str_length(int dummy);
int str_is_digit(int ch);
int str_is_alpha(int ch);
int str_to_upper(int ch);
int str_to_lower(int ch);
int str_char_count(int ch, int limit);

#endif /* STRING_UTILS_H */
