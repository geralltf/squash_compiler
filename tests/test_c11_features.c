/*
 * test_c11_features.c — comprehensive test for C11/C17 features in squash compiler
 *
 * Covers:
 *   - Variable-length arrays (VLAs)
 *   - Binary literals
 *   - constexpr variables
 *   - typeof()
 *   - _Static_assert / static_assert
 *   - Enums with underlying type (enum E : int)
 *   - char8_t, char16_t, char32_t
 *   - UTF-8, wide, and unicode character / string literals
 *   - nullptr
 *   - _Noreturn / noreturn
 *   - _Alignas / _Alignof / alignas / alignof
 *   - _Bool / bool
 *   - Decimal float types (_Decimal32 / _Decimal64)
 *   - assert.h (runtime assert)
 *   - Bit-width integers (stdint.h)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <stdnoreturn.h>
#include <stddef.h>
#include "uchar.h"
#include "decimal.h"

/* ============================================================
 * Test infrastructure
 * ============================================================ */
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(label, cond) do { \
    if (cond) { printf("[PASS] %s\n", label); g_pass++; } \
    else       { printf("[FAIL] %s\n", label); g_fail++; } \
} while(0)

/* ============================================================
 * 1. Variable-Length Arrays (VLAs)
 * ============================================================ */
static int vla_sum(int n) {
    int arr[n];
    int i;
    for (i = 0; i < n; i++) arr[i] = i + 1;
    int sum = 0;
    for (i = 0; i < n; i++) sum += arr[i];
    return sum;
}

static void test_vla(void) {
    printf("\n--- VLAs ---\n");
    int s5  = vla_sum(5);   /* 1+2+3+4+5 = 15 */
    int s10 = vla_sum(10);  /* 55 */
    CHECK("VLA sum(5)==15",  s5  == 15);
    CHECK("VLA sum(10)==55", s10 == 55);

    int n = 4;
    int grid[n][n];
    int r, c;
    for (r = 0; r < n; r++)
        for (c = 0; c < n; c++)
            grid[r][c] = r * n + c;
    CHECK("2D VLA [2][3]==11", grid[2][3] == 11);
}

/* ============================================================
 * 2. Binary Literals
 * ============================================================ */
static void test_binary_literals(void) {
    printf("\n--- Binary literals ---\n");
    int a = 0b1010;   /* 10 */
    int b = 0B1111;   /* 15 */
    int c = 0b0000;   /* 0  */
    int d = 0b11111111; /* 255 */
    CHECK("0b1010 == 10",  a == 10);
    CHECK("0B1111 == 15",  b == 15);
    CHECK("0b0000 == 0",   c == 0);
    CHECK("0b11111111 == 255", d == 255);
    CHECK("0b1010 | 0b0101 == 15", (0b1010 | 0b0101) == 15);
    CHECK("0b1100 & 0b1010 == 8",  (0b1100 & 0b1010) == 8);
}

/* ============================================================
 * 3. constexpr variables
 * ============================================================ */
constexpr int MAX_SIZE = 128;
constexpr double PI = 3.14159265358979;

static void test_constexpr(void) {
    printf("\n--- constexpr ---\n");
    CHECK("constexpr MAX_SIZE == 128", MAX_SIZE == 128);
    /* PI is approximately 3.14 */
    CHECK("constexpr PI > 3.14", PI > 3.14);
    CHECK("constexpr PI < 3.15", PI < 3.15);
    /* Used in array size */
    constexpr int LOCAL_CAP = 8;
    int buf[LOCAL_CAP];
    buf[0] = 99;
    CHECK("constexpr local in array size", buf[0] == 99 && LOCAL_CAP == 8);
}

/* ============================================================
 * 4. typeof()
 * ============================================================ */
static void test_typeof(void) {
    printf("\n--- typeof ---\n");
    int x = 42;
    typeof(x) y = x * 2;      /* y should be int = 84 */
    typeof(int) z = 100;
    CHECK("typeof(x) y == 84",   y == 84);
    CHECK("typeof(int) z == 100", z == 100);

    double d = 3.14;
    typeof(d) d2 = d + 1.0;   /* d2 should be 4.14 */
    CHECK("typeof(double) d2 > 4.0", d2 > 4.0);
    CHECK("typeof(double) d2 < 5.0", d2 < 5.0);

    /* __typeof__ is an alias */
    __typeof__(x) w = x + 1;
    CHECK("__typeof__(x) w == 43", w == 43);
}

/* ============================================================
 * 5. _Static_assert / static_assert
 * ============================================================ */
/* Compile-time assertions about well-known sizes */
_Static_assert(sizeof(int) >= 4, "int must be at least 4 bytes");
static_assert(sizeof(char) == 1, "char must be 1 byte");

static void test_static_assert(void) {
    printf("\n--- static_assert ---\n");
    /* These are compile-time checks that already passed if we got here */
    CHECK("_Static_assert sizeof(int)>=4 passed", sizeof(int) >= 4);
    CHECK("static_assert sizeof(char)==1 passed", sizeof(char) == 1);

    /* Runtime static_assert (squash evaluates at runtime) */
    static_assert(1 + 1 == 2, "basic arithmetic");
    CHECK("static_assert 1+1==2 passed", 1);
}

/* ============================================================
 * 6. Enums with underlying type
 * ============================================================ */
enum Color : int { RED = 0, GREEN = 1, BLUE = 2 };
enum Flags : unsigned int { FLAG_A = 1, FLAG_B = 2, FLAG_C = 4 };
enum Status : char { STATUS_OK = 0, STATUS_ERR = -1 };

static void test_enum_underlying(void) {
    printf("\n--- Enum with underlying type ---\n");
    enum Color c = GREEN;
    CHECK("enum Color GREEN == 1", c == 1);
    enum Flags f = FLAG_A | FLAG_B;
    CHECK("enum Flags FLAG_A|FLAG_B == 3", f == 3);
    enum Status s = STATUS_ERR;
    CHECK("enum Status STATUS_ERR == -1", s == -1);
}

/* ============================================================
 * 7. char8_t, char16_t, char32_t
 * ============================================================ */
static void test_char_types(void) {
    printf("\n--- char8_t / char16_t / char32_t ---\n");
    char8_t  c8  = 'A';
    char16_t c16 = 'B';
    char32_t c32 = 'C';
    CHECK("char8_t  'A' == 65", (int)c8  == 65);
    CHECK("char16_t 'B' == 66", (int)c16 == 66);
    CHECK("char32_t 'C' == 67", (int)c32 == 67);
    CHECK("sizeof(char8_t)  == 1", sizeof(char8_t)  == 1);
    CHECK("sizeof(char16_t) == 2", sizeof(char16_t) == 2);
    CHECK("sizeof(char32_t) == 4", sizeof(char32_t) == 4);
}

/* ============================================================
 * 8. Wide / unicode string and character literals
 * ============================================================ */
static void test_unicode_literals(void) {
    printf("\n--- Unicode / wide literals ---\n");
    /* UTF-8 string prefix — content is treated as ASCII/UTF-8 */
    const char *s8 = u8"Hello";
    CHECK("u8 string is non-NULL", s8 != 0);
    CHECK("u8 string[0] == 'H'",   s8[0] == 'H');

    /* Wide character literals — prefix skipped, treated as char */
    int wc = L'A';
    CHECK("L'A' == 65", wc == 65);

    /* Wide string — prefix skipped, treated as char* */
    const char *ws = L"World";
    CHECK("L string is non-NULL", ws != 0);
    CHECK("L string[0] == 'W'",   ws[0] == 'W');

    /* u and U string prefixes */
    const char *u16s = u"Test16";
    const char *u32s = U"Test32";
    CHECK("u string is non-NULL", u16s != 0);
    CHECK("U string is non-NULL", u32s != 0);
    CHECK("u string[0] == 'T'",   u16s[0] == 'T');
    CHECK("U string[0] == 'T'",   u32s[0] == 'T');

    /* u8 character literal */
    int u8c = u8'Z';
    CHECK("u8'Z' == 90", u8c == 90);
}

/* ============================================================
 * 9. nullptr
 * ============================================================ */
static void test_nullptr(void) {
    printf("\n--- nullptr ---\n");
    void *p = nullptr;
    CHECK("nullptr is zero", p == 0);
    int *ip = nullptr;
    CHECK("int* nullptr == NULL", ip == 0);
    /* Comparison */
    char *cp = "hello";
    CHECK("non-null != nullptr", cp != nullptr);
}

/* ============================================================
 * 10. _Noreturn / noreturn
 * ============================================================ */
noreturn static void do_exit(int code) {
    exit(code);
}

/* We can't call do_exit in tests (it exits), just verify it compiles */
static void test_noreturn(void) {
    printf("\n--- _Noreturn / noreturn ---\n");
    /* Verify the keyword compiled without error */
    CHECK("noreturn function compiled", 1);
    /* Verify _Noreturn works as keyword */
    _Noreturn void (*fn_ptr)(int) = do_exit;
    (void)fn_ptr;
    CHECK("_Noreturn function pointer compiles", 1);
}

/* ============================================================
 * 11. _Alignas / _Alignof
 * ============================================================ */
static void test_alignment(void) {
    printf("\n--- _Alignas / _Alignof ---\n");
    /* Alignment of basic types */
    int ai = _Alignof(int);
    int ad = _Alignof(double);
    CHECK("_Alignof(int) >= 1",    ai >= 1);
    CHECK("_Alignof(double) >= 1", ad >= 1);
    CHECK("alignof(int) >= 1",     alignof(int) >= 1);

    /* Aligned variable */
    _Alignas(16) char buf[64];
    buf[0] = 42;
    CHECK("_Alignas(16) variable accessible", buf[0] == 42);

    /* alignas alias */
    alignas(8) int aligned_int = 7;
    CHECK("alignas(8) int == 7", aligned_int == 7);
}

/* ============================================================
 * 12. _Bool / bool
 * ============================================================ */
static void test_bool(void) {
    printf("\n--- _Bool / bool ---\n");
    _Bool bt = 1;
    _Bool bf = 0;
    CHECK("_Bool true  == 1", bt == 1);
    CHECK("_Bool false == 0", bf == 0);

    bool b2 = true;
    bool b3 = false;
    CHECK("bool true  == 1", b2 == 1);
    CHECK("bool false == 0", b3 == 0);

    /* Conversion */
    _Bool from_int = 42;
    CHECK("_Bool from nonzero == 1", from_int == 1);
    _Bool from_zero = 0;
    CHECK("_Bool from zero == 0", from_zero == 0);
}

/* ============================================================
 * 13. Decimal float types
 * ============================================================ */
static void test_decimal_floats(void) {
    printf("\n--- Decimal float types ---\n");
    _Decimal32  d32  = 1.5f;
    _Decimal64  d64  = 2.75;
    _Decimal128 d128 = 3.125;
    CHECK("_Decimal32  1.5  > 1.0", d32  > 1.0f);
    CHECK("_Decimal64  2.75 > 2.0", d64  > 2.0);
    CHECK("_Decimal128 3.125 > 3.0", d128 > 3.0);
    CHECK("sizeof(_Decimal32) >= 4",  sizeof(_Decimal32)  >= 4);
    CHECK("sizeof(_Decimal64) >= 8",  sizeof(_Decimal64)  >= 8);
    CHECK("sizeof(_Decimal128) >= 8", sizeof(_Decimal128) >= 8);
}

/* ============================================================
 * 14. assert.h — runtime assertions
 * ============================================================ */
static void test_assert(void) {
    printf("\n--- assert.h ---\n");
    /* These will abort if false, so they're effectively compile-time
     * checks at runtime. They pass if we don't abort. */
    assert(1 == 1);
    assert(2 + 2 == 4);
    assert("string" != 0);
    CHECK("assert(1==1) passed",    1);
    CHECK("assert(2+2==4) passed",  1);
    CHECK("assert(str!=0) passed",  1);
}

/* ============================================================
 * 15. Bit-width integers (stdint.h)
 * ============================================================ */
static void test_stdint(void) {
    printf("\n--- stdint.h bit-width integers ---\n");
    int8_t   i8  = -128;
    uint8_t  u8  = 255;
    int16_t  i16 = -32768;
    uint16_t u16 = 65535;
    int32_t  i32 = -2147483648;
    uint32_t u32 = 4294967295u;
    int64_t  i64 = -1;
    uint64_t u64 = 18446744073709551615ull;

    CHECK("int8_t  -128", i8  == -128);
    CHECK("uint8_t  255", u8  == 255);
    CHECK("int16_t  -32768", i16 == -32768);
    CHECK("uint16_t 65535",  u16 == 65535);
    CHECK("int32_t  min",    i32 == -2147483648);
    CHECK("uint32_t max",    u32 == 4294967295u);
    CHECK("int64_t  -1",     i64 == -1);
    CHECK("uint64_t max",    u64 == 18446744073709551615ull);

    CHECK("sizeof(int8_t)==1",  sizeof(int8_t)  == 1);
    CHECK("sizeof(int16_t)==2", sizeof(int16_t) == 2);
    CHECK("sizeof(int32_t)==4", sizeof(int32_t) == 4);
    CHECK("sizeof(int64_t)==8", sizeof(int64_t) == 8);

    /* intptr_t should be pointer-sized */
    intptr_t  ip  = (intptr_t)&i8;
    uintptr_t uip = (uintptr_t)&i8;
    CHECK("intptr_t non-zero",  ip  != 0);
    CHECK("uintptr_t non-zero", uip != 0);

    /* INT8_MIN / INT8_MAX macros */
    CHECK("INT8_MIN == -128", INT8_MIN == -128);
    CHECK("INT8_MAX == 127",  INT8_MAX == 127);
    CHECK("UINT8_MAX == 255", UINT8_MAX == 255);
    CHECK("INT16_MIN == -32768", INT16_MIN == -32768);
    CHECK("INT16_MAX == 32767",  INT16_MAX == 32767);
    CHECK("INT32_MIN == -2147483648", INT32_MIN == -2147483648);
    CHECK("INT32_MAX == 2147483647",  INT32_MAX == 2147483647);
}

/* ============================================================
 * 16. size_t, ptrdiff_t, nullptr_t (stddef.h)
 * ============================================================ */
static void test_stddef(void) {
    printf("\n--- stddef.h ---\n");
    size_t sz = sizeof(int);
    CHECK("size_t sizeof(int) >= 4", sz >= 4);
    ptrdiff_t diff = 0;
    int arr[3] = {1,2,3};
    diff = &arr[2] - &arr[0];
    CHECK("ptrdiff_t arr[2]-arr[0] == 2", diff == 2);

    /* offsetof */
    typedef struct { char a; int b; double c; } TestStruct;
    size_t off_a = offsetof(TestStruct, a);
    size_t off_c = offsetof(TestStruct, c);
    CHECK("offsetof(a) == 0",  off_a == 0);
    CHECK("offsetof(c) >= 4",  off_c >= 4);
}

/* ============================================================
 * main
 * ============================================================ */
int main(void) {
    printf("=== C11/C17 Feature Tests ===\n");

    test_vla();
    test_binary_literals();
    test_constexpr();
    test_typeof();
    test_static_assert();
    test_enum_underlying();
    test_char_types();
    test_unicode_literals();
    test_nullptr();
    test_noreturn();
    test_alignment();
    test_bool();
    test_decimal_floats();
    test_assert();
    test_stdint();
    test_stddef();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
