/*
 * math_utils.h  —  Test header demonstrating:
 *   - Header guard (#ifndef / #define / #endif)
 *   - Object-like #define macros
 *   - enum declaration
 *   - typedef
 *   - struct declaration
 *   - Forward function declarations (prototypes)
 *   - Nested #include (includes string_utils.h)
 *   - Conditional compilation (#ifdef / #ifndef)
 */

#ifndef MATH_UTILS_H
#define MATH_UTILS_H

/* ── Macros ─────────────────────────────────────────────── */
#define PI_INT      3
#define MAX_VAL     1000
#define MIN_VAL     0
#define SQUARE(x)   x
#define ABS_APPROX  1

/* ── Conditional: extra constant only when DEBUG is not set ─ */
#ifndef DEBUG
#define RELEASE_BUILD 1
#define BUILD_TAG 42
#endif

#ifdef DEBUG
#define BUILD_TAG 0
#endif

/* ── enum ───────────────────────────────────────────────── */
enum MathOp {
    MATH_ADD = 0,
    MATH_SUB = 1,
    MATH_MUL = 2,
    MATH_DIV = 3
};

enum Status {
    STATUS_OK    = 0,
    STATUS_ERROR = 1,
    STATUS_DONE  = 99
};

/* ── typedef ────────────────────────────────────────────── */
typedef int MathResult;
typedef int MathInt;

/* ── Forward declarations (function prototypes) ─────────── */
int math_add(int a, int b);
int math_sub(int a, int b);
int math_mul(int a, int b);
int math_div(int a, int b);
int math_abs(int x);
int math_max(int a, int b);
int math_min(int a, int b);
int math_clamp(int x, int lo, int hi);
int math_apply(int a, int b, int op);

/* Include nested header (tests multi-level include) */
#include "string_utils.h"

#endif /* MATH_UTILS_H */
