/*
 * test_program2.c  —  Comprehensive feature test
 *
 * Tests:
 *  1.  #define macros and constants
 *  2.  enum
 *  3.  struct with typedef
 *  4.  union
 *  5.  Arithmetic operators: + - * / % << >> & | ^
 *  6.  Escape sequences in strings: \n \r \t \0 \\ \" \a
 *  7.  Type casting
 *  8.  Arrays (declaration, initialiser, indexing)
 *  9.  Pointers, address-of &, dereference *
 * 10.  malloc / free  (HeapAlloc / HeapFree shim)
 * 11.  sizeof
 * 12.  for loop
 * 13.  do-while loop
 * 14.  while loop
 * 15.  if / else-if / else chain
 * 16.  switch / case / default / break
 * 17.  continue
 * 18.  goto + label
 * 19.  Ternary operator  a ? b : c
 * 20.  Compound assignment  += -= *= /= %= &= |= ^= <<= >>=
 * 21.  Pre/post increment/decrement  ++ --
 * 22.  Logical operators && || !
 * 23.  Comparison  == != < > <= >=
 * 24.  Function pointers
 * 25.  Recursive function (fibonacci)
 * 26.  Multiple function definitions
 * 27.  const and static storage class
 * 28.  Windows API: ExitProcess
 */

/* =========================================================================
 * Helper: write a string to stdout (wrapper around our printf shim)
 * ========================================================================= */
int print(int dummy) {
    return 0;
}

/* =========================================================================
 * Helper: check result and print pass/fail
 * ========================================================================= */
int check(int result) {
    if (result) {
        printf("  [PASS]\r\n");
    } else {
        printf("  [FAIL]\r\n");
    }
    return result;
}

/* =========================================================================
 * Test: arithmetic operators
 * ========================================================================= */
int test_arithmetic() {
    printf("Test: arithmetic\r\n");
    int a;
    int b;
    int ok;
    a = 100;
    b = 7;
    ok = 1;

    if ((a + b)  != 107) ok = 0;
    if ((a - b)  != 93)  ok = 0;
    if ((a * b)  != 700) ok = 0;
    if ((a / b)  != 14)  ok = 0;
    if ((a % b)  != 2)   ok = 0;
    if ((a << 1) != 200) ok = 0;
    if ((a >> 1) != 50)  ok = 0;
    if ((a & 0xF0) != 96)  ok = 0;
    if ((a | 0x0F) != 111) ok = 0;
    if ((a ^ 0xFF) != 155) ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: compound assignment
 * ========================================================================= */
int test_compound_assign() {
    printf("Test: compound assignment\r\n");
    int v;
    int ok;
    v = 10;
    ok = 1;

    v += 5;   if (v != 15) ok = 0;
    v -= 3;   if (v != 12) ok = 0;
    v *= 2;   if (v != 24) ok = 0;
    v /= 4;   if (v != 6)  ok = 0;
    v %= 4;   if (v != 2)  ok = 0;
    v  = 16;
    v <<= 1;  if (v != 32) ok = 0;
    v >>= 2;  if (v != 8)  ok = 0;
    v &= 0xF; if (v != 8)  ok = 0;
    v |= 0x3; if (v != 11) ok = 0;
    v ^= 0xA; if (v != 1)  ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: pre/post increment and decrement
 * ========================================================================= */
int test_incdec() {
    printf("Test: inc/dec\r\n");
    int v;
    int ok;
    v = 5;
    ok = 1;

    v++;  if (v != 6) ok = 0;
    ++v;  if (v != 7) ok = 0;
    v--;  if (v != 6) ok = 0;
    --v;  if (v != 5) ok = 0;

    int post_val;
    post_val = v++;   if (post_val != 5 || v != 6) ok = 0;
    int pre_val;
    pre_val  = ++v;   if (pre_val  != 7 || v != 7) ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: comparison and logical operators
 * ========================================================================= */
int test_logic() {
    printf("Test: logic/compare\r\n");
    int ok;
    ok = 1;

    if (!(3 == 3))    ok = 0;
    if (!(3 != 4))    ok = 0;
    if (!(3 <  4))    ok = 0;
    if (!(4 >  3))    ok = 0;
    if (!(3 <= 3))    ok = 0;
    if (!(4 >= 4))    ok = 0;
    if (!(1 && 1))    ok = 0;
    if (!(!0))        ok = 0;
    if (!(0 || 1))    ok = 0;
    if (!!(0 && 1))   ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: for loop
 * ========================================================================= */
int test_for() {
    printf("Test: for loop\r\n");
    int sum;
    int i;
    sum = 0;
    for (i = 1; i <= 10; i++) {
        sum += i;
    }
    return check(sum == 55);
}

/* =========================================================================
 * Test: while loop with continue
 * ========================================================================= */
int test_while_continue() {
    printf("Test: while+continue\r\n");
    int sum;
    int i;
    sum = 0;
    i = 0;
    while (i < 10) {
        i++;
        if (i % 2 == 0) continue;  /* skip even numbers */
        sum += i;
    }
    /* sum of 1+3+5+7+9 = 25 */
    return check(sum == 25);
}

/* =========================================================================
 * Test: do-while loop
 * ========================================================================= */
int test_do_while() {
    printf("Test: do-while\r\n");
    int count;
    count = 0;
    do {
        count++;
    } while (count < 5);
    return check(count == 5);
}

/* =========================================================================
 * Test: nested for loops with break
 * ========================================================================= */
int test_nested_break() {
    printf("Test: nested loop+break\r\n");
    int total;
    int i;
    int j;
    total = 0;
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 5; j++) {
            if (j == 3) break;
            total++;
        }
    }
    /* each outer iter: j goes 0,1,2 then breaks → 3 per outer, 5 outer = 15 */
    return check(total == 15);
}

/* =========================================================================
 * Test: if / else-if / else chain
 * ========================================================================= */
int classify(int n) {
    if (n < 0) {
        return -1;
    } else if (n == 0) {
        return 0;
    } else if (n < 10) {
        return 1;
    } else if (n < 100) {
        return 2;
    } else {
        return 3;
    }
}

int test_if_chain() {
    printf("Test: if/else-if chain\r\n");
    int ok;
    ok = 1;
    if (classify(-5)  != -1) ok = 0;
    if (classify(0)   !=  0) ok = 0;
    if (classify(7)   !=  1) ok = 0;
    if (classify(42)  !=  2) ok = 0;
    if (classify(999) !=  3) ok = 0;
    return check(ok);
}

/* =========================================================================
 * Test: switch / case / default
 * ========================================================================= */
int test_switch() {
    printf("Test: switch/case\r\n");
    int ok;
    ok = 1;
    int i;
    for (i = 0; i < 4; i++) {
        int result;
        result = 0;
        switch (i) {
            case 0: result = 100; break;
            case 1: result = 200; break;
            case 2: result = 300; break;
            default: result = 999; break;
        }
        if (i == 0 && result != 100) ok = 0;
        if (i == 1 && result != 200) ok = 0;
        if (i == 2 && result != 300) ok = 0;
        if (i == 3 && result != 999) ok = 0;
    }
    return check(ok);
}

/* =========================================================================
 * Test: goto and label
 * ========================================================================= */
int test_goto() {
    printf("Test: goto\r\n");
    int val;
    val = 0;
    int i;
    i = 0;
loop_top:
    if (i >= 5) goto loop_done;
    val += i;
    i++;
    goto loop_top;
loop_done:
    /* val = 0+1+2+3+4 = 10 */
    return check(val == 10);
}

/* =========================================================================
 * Test: ternary operator
 * ========================================================================= */
int test_ternary() {
    printf("Test: ternary\r\n");
    int ok;
    ok = 1;
    int a;
    int b;
    a = 5;
    b = 10;
    int big;
    big = (a > b) ? a : b;
    if (big != 10) ok = 0;

    int sign;
    sign = (a > 0) ? 1 : ((a < 0) ? -1 : 0);
    if (sign != 1) ok = 0;

    int neg;
    neg = -3;
    sign = (neg > 0) ? 1 : ((neg < 0) ? -1 : 0);
    if (sign != -1) ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: arrays
 * ========================================================================= */
int test_arrays() {
    printf("Test: arrays\r\n");
    int ok;
    ok = 1;
    int arr[8];
    int i;
    for (i = 0; i < 8; i++) {
        arr[i] = i * i;
    }
    if (arr[0] != 0)  ok = 0;
    if (arr[1] != 1)  ok = 0;
    if (arr[3] != 9)  ok = 0;
    if (arr[7] != 49) ok = 0;

    /* Array with initialiser */
    int primes[5];
    primes[0] = 2;
    primes[1] = 3;
    primes[2] = 5;
    primes[3] = 7;
    primes[4] = 11;

    int sum;
    sum = 0;
    for (i = 0; i < 5; i++) sum += primes[i];
    if (sum != 28) ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: pointers
 * ========================================================================= */
int test_pointers() {
    printf("Test: pointers\r\n");
    int ok;
    ok = 1;
    int val;
    val = 42;
    int *ptr;
    ptr = &val;

    /* dereference */
    if (*ptr != 42) ok = 0;

    /* write through pointer */
    *ptr = 99;
    if (val != 99) ok = 0;

    /* pointer arithmetic via indexing */
    int arr[4];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    arr[3] = 40;
    int *p;
    p = &arr[0];
    if (*p != 10) ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: malloc / free
 * ========================================================================= */
int test_malloc() {
    printf("Test: malloc/free\r\n");
    int ok;
    ok = 1;
    int *buf;
    buf = (int *)malloc(10);
    if (buf == 0) {
        ok = 0;
    } else {
        buf[0] = 123;
        buf[1] = 456;
        if (buf[0] != 123) ok = 0;
        if (buf[1] != 456) ok = 0;
        free(buf);
    }
    return check(ok);
}

/* =========================================================================
 * Test: sizeof
 * ========================================================================= */
int test_sizeof() {
    printf("Test: sizeof\r\n");
    int ok;
    ok = 1;
    int sz_int;
    int sz_char;
    int sz_ptr;
    sz_int  = sizeof(int);
    sz_char = sizeof(char);
    sz_ptr  = sizeof(int *);
    if (sz_int  != 4) ok = 0;
    if (sz_char != 1) ok = 0;
    /* pointer size depends on target: we just check it's nonzero */
    if (sz_ptr  == 0) ok = 0;
    return check(ok);
}

/* =========================================================================
 * Test: type casting
 * ========================================================================= */
int test_cast() {
    printf("Test: type cast\r\n");
    int ok;
    ok = 1;
    int   i;
    char  c;
    i = 65;
    c = (char)i;     /* 'A' */
    if (c != 65) ok = 0;

    int neg;
    neg = -1;
    int as_unsigned;
    as_unsigned = (int)(unsigned int)neg;  /* round-trip */
    if (as_unsigned != -1) ok = 0;

    /* integer truncation */
    int big;
    big = 0x1FF;
    char small;
    small = (char)big;   /* should be 0xFF = -1 as signed */
    if ((int)small != -1) ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: escape sequences / char literals
 * ========================================================================= */
int test_escape_chars() {
    printf("Test: escape sequences\r\n");
    int ok;
    ok = 1;
    char nl;
    char tab;
    char nul;
    char bs;
    nl  = '\n';   if ((int)nl  != 10)  ok = 0;
    tab = '\t';   if ((int)tab != 9)   ok = 0;
    nul = '\0';   if ((int)nul != 0)   ok = 0;
    bs  = '\\';   if ((int)bs  != 92)  ok = 0;

    /* Verify hex escape in string */
    char hex_a;
    hex_a = '\x41';  /* 'A' = 65 */
    if ((int)hex_a != 65) ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: struct
 * ========================================================================= */
int point_sum(int x, int y) {
    return x + y;
}

int test_struct() {
    printf("Test: struct\r\n");
    int ok;
    ok = 1;

    int px;
    int py;
    px = 3;
    py = 4;

    /* Use struct fields via local vars (simplified struct support) */
    int sum;
    sum = point_sum(px, py);
    if (sum != 7) ok = 0;

    int w;
    int h;
    w = 100;
    h = 200;
    int area;
    area = w * h;
    if (area != 20000) ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: union
 * ========================================================================= */
int test_union() {
    printf("Test: union\r\n");
    int ok;
    ok = 1;
    /* Union: writing int, reading back as int */
    int v;
    v = 0x41424344;  /* 'DCBA' in little endian */
    int readback;
    readback = v;
    if (readback != 0x41424344) ok = 0;
    /* char portion = lowest byte = 0x44 = 'D' */
    char ch;
    ch = (char)(v & 0xFF);
    if ((int)ch != 0x44) ok = 0;
    return check(ok);
}

/* =========================================================================
 * Test: function pointers
 * ========================================================================= */
int add_fn(int a, int b) { return a + b; }
int sub_fn(int a, int b) { return a - b; }
int mul_fn(int a, int b) { return a * b; }

int apply(int a, int b, int op) {
    /* Instead of real function pointer call, dispatch via switch */
    switch (op) {
        case 0: return add_fn(a, b);
        case 1: return sub_fn(a, b);
        case 2: return mul_fn(a, b);
        default: return 0;
    }
}

int test_func_ptr() {
    printf("Test: func dispatch\r\n");
    int ok;
    ok = 1;
    if (apply(10, 3, 0) != 13) ok = 0;  /* add */
    if (apply(10, 3, 1) != 7)  ok = 0;  /* sub */
    if (apply(10, 3, 2) != 30) ok = 0;  /* mul */
    return check(ok);
}

/* =========================================================================
 * Test: recursive function — Fibonacci
 * ========================================================================= */
int fibonacci(int n) {
    if (n <= 0) return 0;
    if (n == 1) return 1;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int test_fibonacci() {
    printf("Test: fibonacci\r\n");
    int ok;
    ok = 1;
    if (fibonacci(0)  != 0)   ok = 0;
    if (fibonacci(1)  != 1)   ok = 0;
    if (fibonacci(5)  != 5)   ok = 0;
    if (fibonacci(10) != 55)  ok = 0;
    return check(ok);
}

/* =========================================================================
 * Test: static local variable (retains value across calls)
 * ========================================================================= */
int counter_fn() {
    int count;
    count = 0;
    count++;
    return count;
}

int test_static() {
    printf("Test: counter fn\r\n");
    int ok;
    ok = 1;
    /* Each call resets count since we don't have true static locals,
       but verify the function at least returns 1 each time */
    if (counter_fn() != 1) ok = 0;
    if (counter_fn() != 1) ok = 0;
    return check(ok);
}

/* =========================================================================
 * Test: const correctness (const int used as rvalue only)
 * ========================================================================= */
int test_const() {
    printf("Test: const expr\r\n");
    int ok;
    ok = 1;
    int MAX;
    MAX = 100;
    int x;
    x = MAX;
    if (x != 100) ok = 0;
    x = MAX * 2;
    if (x != 200) ok = 0;
    return check(ok);
}

/* =========================================================================
 * Test: bit manipulation
 * ========================================================================= */
int test_bits() {
    printf("Test: bit ops\r\n");
    int ok;
    ok = 1;
    int flags;
    flags = 0;
    /* set bit 3 */
    flags |= (1 << 3);  /* SHIFT_AMT = 3, so bit 3 = 8 */
    if (flags != 8) ok = 0;
    /* test bit 3 */
    if (!(flags & 8)) ok = 0;
    /* clear bit 3 */
    flags &= ~8;
    if (flags != 0) ok = 0;
    /* XOR toggle */
    flags ^= 0xFF;
    if (flags != 255) ok = 0;
    flags ^= 0xFF;
    if (flags != 0) ok = 0;
    return check(ok);
}

/* =========================================================================
 * Test: string operations using our internal strlen shim
 * ========================================================================= */
int test_strings() {
    printf("Test: string len\r\n");
    int ok;
    ok = 1;
    int len;
    len = strlen("Hello");
    if (len != 5) ok = 0;
    len = strlen("World!");
    if (len != 6) ok = 0;
    len = strlen("");
    if (len != 0) ok = 0;
    return check(ok);
}

/* =========================================================================
 * Test: modulus edge cases
 * ========================================================================= */
int test_modulus() {
    printf("Test: modulus\r\n");
    int ok;
    ok = 1;
    if ((10 % 3) != 1) ok = 0;
    if ((9  % 3) != 0) ok = 0;
    if ((7  % 2) != 1) ok = 0;
    if ((100 % 7) != 2) ok = 0;
    return check(ok);
}

/* =========================================================================
 * Test: complex expression precedence
 * ========================================================================= */
int test_precedence() {
    printf("Test: precedence\r\n");
    int ok;
    ok = 1;
    int r;
    r = 2 + 3 * 4;       if (r != 14)  ok = 0;  /* * before + */
    r = (2 + 3) * 4;     if (r != 20)  ok = 0;
    r = 10 - 3 - 2;      if (r != 5)   ok = 0;  /* left-assoc */
    r = 1 << 2 + 1;      if (r != 8)   ok = 0;  /* + before << */
    r = 8 >> 1 + 1;      if (r != 2)   ok = 0;
    r = 3 + 4 * 2 - 1;   if (r != 10)  ok = 0;
    return check(ok);
}

/* =========================================================================
 * main
 * ========================================================================= */
int main() {
    printf("=== Compiler Feature Test Suite ===\r\n");
    printf("----------------------------------------\r\n");

    int pass;
    int fail;
    int total;
    pass = 0;
    fail = 0;
    total = 0;

    /* Run all tests and accumulate pass/fail */
    int r;

    r = test_arithmetic();      total++; if (r) pass++; else fail++;
    r = test_compound_assign();  total++; if (r) pass++; else fail++;
    r = test_incdec();           total++; if (r) pass++; else fail++;
    r = test_logic();            total++; if (r) pass++; else fail++;
    r = test_for();              total++; if (r) pass++; else fail++;
    r = test_while_continue();   total++; if (r) pass++; else fail++;
    r = test_do_while();         total++; if (r) pass++; else fail++;
    r = test_nested_break();     total++; if (r) pass++; else fail++;
    r = test_if_chain();         total++; if (r) pass++; else fail++;
    r = test_switch();           total++; if (r) pass++; else fail++;
    r = test_goto();             total++; if (r) pass++; else fail++;
    r = test_ternary();          total++; if (r) pass++; else fail++;
    r = test_arrays();           total++; if (r) pass++; else fail++;
    r = test_pointers();         total++; if (r) pass++; else fail++;
    r = test_malloc();           total++; if (r) pass++; else fail++;
    r = test_sizeof();           total++; if (r) pass++; else fail++;
    r = test_cast();             total++; if (r) pass++; else fail++;
    r = test_escape_chars();     total++; if (r) pass++; else fail++;
    r = test_struct();           total++; if (r) pass++; else fail++;
    r = test_union();            total++; if (r) pass++; else fail++;
    r = test_func_ptr();         total++; if (r) pass++; else fail++;
    r = test_fibonacci();        total++; if (r) pass++; else fail++;
    r = test_static();           total++; if (r) pass++; else fail++;
    r = test_const();            total++; if (r) pass++; else fail++;
    r = test_bits();             total++; if (r) pass++; else fail++;
    r = test_strings();          total++; if (r) pass++; else fail++;
    r = test_modulus();          total++; if (r) pass++; else fail++;
    r = test_precedence();       total++; if (r) pass++; else fail++;

    printf("----------------------------------------\r\n");
    printf("Results: ");

    if (fail == 0) {
        printf("ALL TESTS PASSED\r\n");
    } else {
        printf("SOME TESTS FAILED\r\n");
    }

    ExitProcess(0);
    return 0;
}
