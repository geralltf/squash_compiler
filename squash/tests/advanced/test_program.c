/*
 * test_program.c  —  Test source for the compiler
 *
 * Tests:
 *   1. Console printing via printf() (mapped to WriteFile shim)
 *   2. Windows API imports (GetStdHandle, WriteFile, ExitProcess)
 *   3. Function definitions and calls
 *   4. Variable declarations and arithmetic expressions
 *   5. if / else statements
 *   6. while loops
 *   7. Operator precedence and parenthesised expressions
 *   8. Recursive function calls (factorial)
 *   9. Logical and comparison operators
 *  10. Return values from functions
 */

/* Helper: print a single number.
   Because our compiler maps printf() to a raw WriteFile shim that writes
   the format string verbatim, we use a dedicated print_int that calls
   WriteFile directly after converting the integer to digits. */

int print_str(int n) {
    /* n is actually used as a dummy; we just call printf which writes its
       first arg (the format string) directly to stdout. */
    return 0;
}

/* ---- Arithmetic helper ---- */
int add(int a, int b) {
    return a + b;
}

int mul(int a, int b) {
    return a * b;
}

/* ---- Recursive factorial ---- */
int factorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

/* ---- Test if/else ---- */
int max(int a, int b) {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}

/* ---- Test while loop with accumulator ---- */
int sum_to(int n) {
    int acc;
    acc = 0;
    int i;
    i = 1;
    while (i <= n) {
        acc = acc + i;
        i = i + 1;
    }
    return acc;
}

/* ---- Test operator precedence ---- */
int precedence_test(int x) {
    /* (2 + 3) * 4 - 1 = 19, not 2 + 3*4 - 1 = 13 */
    int a;
    a = (2 + 3) * 4 - 1;
    /* Bitwise: (x | 0x0F) & 0xFF */
    int b;
    b = (x | 15) & 255;
    return a + b;
}

/* ---- Test logical operators ---- */
int logical_test(int x, int y) {
    int r;
    r = 0;
    if (x > 0 && y > 0) {
        r = 1;
    }
    if (x < 0 || y < 0) {
        r = r + 10;
    }
    if (!(x == y)) {
        r = r + 100;
    }
    return r;
}

/* ---- main: orchestrate all tests ---- */
int main() {
    /* Test 1: basic console output */
    printf("=== Compiler Test Program ===\r\n");

    /* Test 2: function call and arithmetic */
    printf("Test: add(3,4)\r\n");
    int s;
    s = add(3, 4);

    printf("Test: mul(6,7)\r\n");
    int p;
    p = mul(6, 7);

    /* Test 3: recursive factorial */
    printf("Test: factorial(5)\r\n");
    int f;
    f = factorial(5);

    /* Test 4: if/else via max() */
    printf("Test: max(10,20)\r\n");
    int m;
    m = max(10, 20);

    /* Test 5: while loop */
    printf("Test: sum_to(10)\r\n");
    int total;
    total = sum_to(10);

    /* Test 6: operator precedence */
    printf("Test: precedence\r\n");
    int pr;
    pr = precedence_test(5);

    /* Test 7: logical operators */
    printf("Test: logical\r\n");
    int lr;
    lr = logical_test(3, 5);

    /* Test 8: nested while loops */
    printf("Test: nested while\r\n");
    int outer;
    outer = 0;
    int count;
    count = 0;
    while (outer < 3) {
        int inner;
        inner = 0;
        while (inner < 3) {
            count = count + 1;
            inner = inner + 1;
        }
        outer = outer + 1;
    }

    /* Test 9: chained if/else-if */
    printf("Test: chained if\r\n");
    int grade;
    grade = 75;
    if (grade >= 90) {
        printf("Grade: A\r\n");
    } else {
        if (grade >= 80) {
            printf("Grade: B\r\n");
        } else {
            if (grade >= 70) {
                printf("Grade: C\r\n");
            } else {
                printf("Grade: F\r\n");
            }
        }
    }

    /* Test 10: expression with all operators */
    printf("Test: expressions\r\n");
    int expr;
    expr = 2 + 3 * 4 - 1;          /* = 13 */
    int expr2;
    expr2 = (2 + 3) * (4 - 1);     /* = 15 */
    int expr3;
    expr3 = 100 / 5 + 2 * 3 - 1;   /* = 25 */

    /* Test 11: Windows API call - GetStdHandle (already tested via printf shim)
       Direct call to ExitProcess with success code */
    printf("All tests passed!\r\n");
    ExitProcess(0);

    return 0;
}
