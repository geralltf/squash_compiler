#ifndef _ASSERT_H
#define _ASSERT_H

void printf(const char *fmt, ...);
void exit(int code);

/* Runtime assert: print message and abort */
#define assert(expr) \
    do { if (!(expr)) { printf("Assertion failed: " #expr "\n"); exit(1); } } while(0)

/* C11 static_assert — simplified: evaluate condition, abort at runtime if false */
#define static_assert(expr, msg) \
    do { if (!(expr)) { printf("static_assert failed: " msg "\n"); exit(1); } } while(0)

/* C11 _Static_assert alias */
#define _Static_assert(expr, msg) static_assert(expr, msg)

/* NDEBUG: disable assert in release builds */
#ifdef NDEBUG
#undef  assert
#define assert(expr) ((void)0)
#endif

#endif /* _ASSERT_H */
