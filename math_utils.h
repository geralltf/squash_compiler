/* math_utils.h — helper used by test_program2.c */
#ifndef MATH_UTILS_H
#define MATH_UTILS_H
static inline int square(int x) { return x * x; }
static inline int cube(int x) { return x * x * x; }
static inline int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}
#endif

/* Operation codes for math_apply */
#define MATH_ADD 0
#define MATH_SUB 1
#define MATH_MUL 2
#define MATH_DIV 3
