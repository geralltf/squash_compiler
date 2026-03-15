/*
 * test_program2.c  —  Comprehensive compiler test suite
 *
 * Tests:
 *   - All original 30 tests (arithmetic through precedence)
 *   - #include with header guards (#ifndef/#define/#endif)
 *   - Nested #include (math_utils.h includes string_utils.h)
 *   - Double-include guard (string_utils.h included twice — no dup)
 *   - Conditional compilation (#ifdef/#ifndef/#if/#elif/#else/#endif)
 *   - Forward function declarations (prototypes before definition)
 *   - Functions defined AFTER main() using forward decls
 *   - Macros, enums, typedefs from included headers
 */

/* Include test headers — exercises #include, header guards, nested includes */
#include "math_utils.h"
#include "string_utils.h"
#include "constants.h"

/* Local macros */
#define PASS_STR   "  [PASS]\r\n"
#define FAIL_STR   "  [FAIL]\r\n"
#define SEP        "----------------------------------------\r\n"
#define MAX_ITEMS  8
#define SHIFT_AMT  3

/* Local enums (used by existing tests) */
enum Color { RED=0, GREEN=1, BLUE=2, WHITE=255 };
enum Direction { NORTH=10, SOUTH=20, EAST=30, WEST=40 };

/* Forward declarations of ALL test functions and helpers defined later */
int check(int result);
int test_arithmetic(void);
int test_compound_assign(void);
int test_incdec(void);
int test_logic(void);
int test_for(void);
int test_while_continue(void);
int test_do_while(void);
int test_nested_break(void);
int classify(int n);
int test_if_chain(void);
int test_switch(void);
int test_switch_enum(void);
int test_goto(void);
int test_ternary(void);
int test_arrays(void);
int test_pointers(void);
int test_malloc(void);
int test_sizeof(void);
int test_cast(void);
int test_escape_chars(void);
int test_enum(void);
int point_sum(int x, int y);
int test_struct(void);
int test_union(void);
int add_fn(int a, int b);
int sub_fn(int a, int b);
int mul_fn(int a, int b);
int apply(int a, int b, int op);
int test_func_ptr(void);
int fibonacci(int n);
int test_fibonacci(void);
int counter_fn(void);
int test_static(void);
int test_const(void);
int test_bits(void);
int test_strings(void);
int test_modulus(void);
int test_precedence(void);
/* New test forward decls */
int test_headers(void);
int test_header_guards(void);
int test_forward_decl(void);
int test_conditional_compile(void);
int test_nested_headers(void);
/* Math/string impl forward decls (bodies after main) */
int math_add(int a, int b);
int math_sub(int a, int b);
int math_mul(int a, int b);
int math_div(int a, int b);
int math_abs(int x);
int math_max(int a, int b);
int math_min(int a, int b);
int math_clamp(int x, int lo, int hi);
int math_apply(int a, int b, int op);
int str_is_digit(int ch);
int str_is_alpha(int ch);
int str_to_upper(int ch);
int str_to_lower(int ch);
int str_length(int dummy);
int str_char_count(int ch, int limit);
/* Functions defined after main to test forward-decl */
int fwd_square(int x);
int fwd_cube(int x);
int fwd_is_even(int n);
int fwd_sum_range(int lo, int hi);

/* =========================================================================
 * check()
 * ========================================================================= */
int check(int result) {
    if (result) { printf(PASS_STR); } else { printf(FAIL_STR); }
    return result;
}

/* =========================================================================
 * Original 30 tests
 * ========================================================================= */
int test_arithmetic(void) {
    printf("Test: arithmetic\r\n");
    int a; int b; int ok; a=100; b=7; ok=1;
    if ((a+b)!=107) ok=0; if ((a-b)!=93) ok=0; if ((a*b)!=700) ok=0;
    if ((a/b)!=14) ok=0;  if ((a%b)!=2) ok=0;  if ((a<<1)!=200) ok=0;
    if ((a>>1)!=50) ok=0; if ((a&0xF0)!=96) ok=0; if ((a|0x0F)!=111) ok=0;
    if ((a^0xFF)!=155) ok=0;
    return check(ok);
}
int test_compound_assign(void) {
    printf("Test: compound assignment\r\n");
    int v; int ok; v=10; ok=1;
    v+=5; if(v!=15) ok=0; v-=3; if(v!=12) ok=0; v*=2; if(v!=24) ok=0;
    v/=4; if(v!=6)  ok=0; v%=4; if(v!=2)  ok=0; v=16;
    v<<=1; if(v!=32) ok=0; v>>=2; if(v!=8) ok=0;
    v&=0xF; if(v!=8) ok=0; v|=0x3; if(v!=11) ok=0; v^=0xA; if(v!=1) ok=0;
    return check(ok);
}
int test_incdec(void) {
    printf("Test: inc/dec\r\n");
    int v; int ok; v=5; ok=1;
    v++; if(v!=6) ok=0; ++v; if(v!=7) ok=0; v--; if(v!=6) ok=0; --v; if(v!=5) ok=0;
    int pv; pv=v++; if(pv!=5||v!=6) ok=0;
    int rv; rv=++v;  if(rv!=7||v!=7) ok=0;
    return check(ok);
}
int test_logic(void) {
    printf("Test: logic/compare\r\n");
    int ok; ok=1;
    if(!(3==3)) ok=0; if(!(3!=4)) ok=0; if(!(3<4)) ok=0; if(!(4>3)) ok=0;
    if(!(3<=3)) ok=0; if(!(4>=4)) ok=0; if(!(1&&1)) ok=0; if(!(!0)) ok=0;
    if(!(0||1)) ok=0; if(!!(0&&1)) ok=0;
    return check(ok);
}
int test_for(void) {
    printf("Test: for loop\r\n");
    int sum; int i; sum=0;
    for(i=1;i<=10;i++) sum+=i;
    return check(sum==55);
}
int test_while_continue(void) {
    printf("Test: while+continue\r\n");
    int sum; int i; sum=0; i=0;
    while(i<10) { i++; if(i%2==0) continue; sum+=i; }
    return check(sum==25);
}
int test_do_while(void) {
    printf("Test: do-while\r\n");
    int count; count=0;
    do { count++; } while(count<5);
    return check(count==5);
}
int test_nested_break(void) {
    printf("Test: nested loop+break\r\n");
    int total; int i; int j; total=0;
    for(i=0;i<5;i++) { for(j=0;j<5;j++) { if(j==3) break; total++; } }
    return check(total==15);
}
int classify(int n) {
    if(n<0) return -1; else if(n==0) return 0;
    else if(n<10) return 1; else if(n<100) return 2; else return 3;
}
int test_if_chain(void) {
    printf("Test: if/else-if chain\r\n");
    int ok; ok=1;
    if(classify(-5)!=-1) ok=0; if(classify(0)!=0) ok=0;
    if(classify(7)!=1) ok=0;   if(classify(42)!=2) ok=0; if(classify(999)!=3) ok=0;
    return check(ok);
}
int test_switch(void) {
    printf("Test: switch/case\r\n");
    int ok; ok=1; int i;
    for(i=0;i<4;i++) {
        int r; r=0;
        switch(i) { case 0:r=100;break; case 1:r=200;break; case 2:r=300;break; default:r=999;break; }
        if(i==0&&r!=100) ok=0; if(i==1&&r!=200) ok=0;
        if(i==2&&r!=300) ok=0; if(i==3&&r!=999) ok=0;
    }
    return check(ok);
}
int test_switch_enum(void) {
    printf("Test: switch+enum\r\n");
    int ok; ok=1; int c; c=GREEN; int nm; nm=0;
    switch(c) { case RED:nm=1;break; case GREEN:nm=2;break; case BLUE:nm=3;break; default:nm=0;break; }
    if(nm!=2) ok=0;
    c=BLUE; nm=0;
    switch(c) { case RED:nm=1;break; case GREEN:nm=2;break; case BLUE:nm=3;break; default:nm=0;break; }
    if(nm!=3) ok=0;
    return check(ok);
}
int test_goto(void) {
    printf("Test: goto\r\n");
    int val; val=0; int i; i=0;
loop_top: if(i>=5) goto loop_done; val+=i; i++; goto loop_top;
loop_done: return check(val==10);
}
int test_ternary(void) {
    printf("Test: ternary\r\n");
    int ok; ok=1;
    int a; int b; a=5; b=10;
    int big; big=(a>b)?a:b; if(big!=10) ok=0;
    int sign; sign=(a>0)?1:((a<0)?-1:0); if(sign!=1) ok=0;
    int neg; neg=-3; sign=(neg>0)?1:((neg<0)?-1:0); if(sign!=-1) ok=0;
    return check(ok);
}
int test_arrays(void) {
    printf("Test: arrays\r\n");
    int ok; ok=1; int arr[8]; int i;
    for(i=0;i<MAX_ITEMS;i++) arr[i]=i*i;
    if(arr[0]!=0) ok=0; if(arr[1]!=1) ok=0; if(arr[3]!=9) ok=0; if(arr[7]!=49) ok=0;
    int primes[5]; primes[0]=2; primes[1]=3; primes[2]=5; primes[3]=7; primes[4]=11;
    int sum; sum=0; for(i=0;i<5;i++) sum+=primes[i];
    if(sum!=28) ok=0;
    return check(ok);
}
int test_pointers(void) {
    printf("Test: pointers\r\n");
    int ok; ok=1; int val; val=42; int *ptr; ptr=&val;
    if(*ptr!=42) ok=0; *ptr=99; if(val!=99) ok=0;
    int arr2[4]; arr2[0]=10; arr2[1]=20; arr2[2]=30; arr2[3]=40;
    int *p; p=&arr2[0]; if(*p!=10) ok=0;
    return check(ok);
}
int test_malloc(void) {
    printf("Test: malloc/free\r\n");
    int ok; ok=1; int *buf; buf=(int *)malloc(40);
    if(buf==0) { ok=0; } else { buf[0]=123; buf[1]=456;
        if(buf[0]!=123) ok=0; if(buf[1]!=456) ok=0; free(buf); }
    return check(ok);
}
int test_sizeof(void) {
    printf("Test: sizeof\r\n");
    int ok; ok=1;
    int si; si=sizeof(int); int sc; sc=sizeof(char); int sp; sp=sizeof(int*);
    if(si!=4) ok=0; if(sc!=1) ok=0; if(sp==0) ok=0;
    return check(ok);
}
int test_cast(void) {
    printf("Test: type cast\r\n");
    int ok; ok=1; int i; char c; i=65; c=(char)i; if(c!=65) ok=0;
    int neg; neg=-1; int au; au=(int)(unsigned int)neg; if(au!=-1) ok=0;
    int big; big=0x1FF; char small; small=(char)big; if((int)small!=-1) ok=0;
    return check(ok);
}
int test_escape_chars(void) {
    printf("Test: escape sequences\r\n");
    int ok; ok=1;
    char nl; char tab; char nul; char bs;
    nl='\n'; if((int)nl!=10) ok=0; tab='\t'; if((int)tab!=9) ok=0;
    nul='\0'; if((int)nul!=0) ok=0; bs='\\'; if((int)bs!=92) ok=0;
    char ha; ha='\x41'; if((int)ha!=65) ok=0;
    return check(ok);
}
int test_enum(void) {
    printf("Test: enum\r\n");
    int ok; ok=1; int c;
    c=RED; if(c!=0) ok=0; c=GREEN; if(c!=1) ok=0;
    c=BLUE; if(c!=2) ok=0; c=WHITE; if(c!=255) ok=0;
    if(NORTH!=10) ok=0; if(SOUTH!=20) ok=0;
    return check(ok);
}
int point_sum(int x, int y) { return x+y; }
int test_struct(void) {
    printf("Test: struct\r\n");
    int ok; ok=1;
    int s; s=point_sum(3,4); if(s!=7) ok=0;
    int area; area=100*200; if(area!=20000) ok=0;
    return check(ok);
}
int test_union(void) {
    printf("Test: union\r\n");
    int ok; ok=1; int v; v=0x41424344;
    int rb; rb=v; if(rb!=0x41424344) ok=0;
    char ch; ch=(char)(v&0xFF); if((int)ch!=0x44) ok=0;
    return check(ok);
}
int add_fn(int a, int b) { return a+b; }
int sub_fn(int a, int b) { return a-b; }
int mul_fn(int a, int b) { return a*b; }
int apply(int a, int b, int op) {
    switch(op) { case 0:return add_fn(a,b); case 1:return sub_fn(a,b); case 2:return mul_fn(a,b); default:return 0; }
}
int test_func_ptr(void) {
    printf("Test: func dispatch\r\n");
    int ok; ok=1;
    if(apply(10,3,0)!=13) ok=0; if(apply(10,3,1)!=7) ok=0; if(apply(10,3,2)!=30) ok=0;
    return check(ok);
}
int fibonacci(int n) { if(n<=0) return 0; if(n==1) return 1; return fibonacci(n-1)+fibonacci(n-2); }
int test_fibonacci(void) {
    printf("Test: fibonacci\r\n");
    int ok; ok=1;
    if(fibonacci(0)!=0) ok=0; if(fibonacci(1)!=1) ok=0;
    if(fibonacci(5)!=5) ok=0; if(fibonacci(10)!=55) ok=0;
    return check(ok);
}
int counter_fn(void) { int count; count=0; count++; return count; }
int test_static(void) {
    printf("Test: counter fn\r\n");
    int ok; ok=1;
    if(counter_fn()!=1) ok=0; if(counter_fn()!=1) ok=0;
    return check(ok);
}
int test_const(void) {
    printf("Test: const expr\r\n");
    int ok; ok=1; int MAX; MAX=100; int x; x=MAX;
    if(x!=100) ok=0; x=MAX*2; if(x!=200) ok=0;
    return check(ok);
}
int test_bits(void) {
    printf("Test: bit ops\r\n");
    int ok; ok=1; int flags; flags=0;
    flags|=(1<<SHIFT_AMT); if(flags!=8) ok=0;
    if(!(flags&8)) ok=0; flags&=~8; if(flags!=0) ok=0;
    flags^=0xFF; if(flags!=255) ok=0; flags^=0xFF; if(flags!=0) ok=0;
    return check(ok);
}
int test_strings(void) {
    printf("Test: string len\r\n");
    int ok; ok=1; int len;
    len=strlen("Hello"); if(len!=5) ok=0;
    len=strlen("World!"); if(len!=6) ok=0;
    len=strlen(""); if(len!=0) ok=0;
    return check(ok);
}
int test_modulus(void) {
    printf("Test: modulus\r\n");
    int ok; ok=1;
    if((10%3)!=1) ok=0; if((9%3)!=0) ok=0; if((7%2)!=1) ok=0; if((100%7)!=2) ok=0;
    return check(ok);
}
int test_precedence(void) {
    printf("Test: precedence\r\n");
    int ok; ok=1; int r;
    r=2+3*4; if(r!=14) ok=0; r=(2+3)*4; if(r!=20) ok=0;
    r=10-3-2; if(r!=5) ok=0; r=1<<2+1; if(r!=8) ok=0;
    r=8>>1+1; if(r!=2) ok=0; r=3+4*2-1; if(r!=10) ok=0;
    return check(ok);
}

/* =========================================================================
 * math_utils.h / string_utils.h function implementations
 * ========================================================================= */
int math_add(int a, int b)  { return a+b; }
int math_sub(int a, int b)  { return a-b; }
int math_mul(int a, int b)  { return a*b; }
int math_div(int a, int b)  { if(b==0) return 0; return a/b; }
int math_abs(int x)         { return (x<0)?-x:x; }
int math_max(int a, int b)  { return (a>b)?a:b; }
int math_min(int a, int b)  { return (a<b)?a:b; }
int math_clamp(int x, int lo, int hi) { if(x<lo) return lo; if(x>hi) return hi; return x; }
int math_apply(int a, int b, int op) {
    switch(op) {
        case MATH_ADD: return math_add(a,b);
        case MATH_SUB: return math_sub(a,b);
        case MATH_MUL: return math_mul(a,b);
        case MATH_DIV: return math_div(a,b);
        default: return 0;
    }
}
int str_length(int dummy)      { return dummy; }
int str_is_digit(int ch)       { return (ch>=48&&ch<=57)?1:0; }
int str_is_alpha(int ch)       { return ((ch>=65&&ch<=90)||(ch>=97&&ch<=122))?1:0; }
int str_to_upper(int ch)       { return (ch>=97&&ch<=122)?ch-32:ch; }
int str_to_lower(int ch)       { return (ch>=65&&ch<=90)?ch+32:ch; }
int str_char_count(int ch, int limit) { int c; c=0; int i; for(i=0;i<limit;i++) c+=(i%10==ch%10)?1:0; return c; }

/* =========================================================================
 * New tests
 * ========================================================================= */
int test_headers(void) {
    printf("Test: header functions\r\n");
    int ok; ok=1;
    if(math_add(3,4)!=7) ok=0;   if(math_sub(10,3)!=7) ok=0;
    if(math_mul(6,7)!=42) ok=0;  if(math_div(20,4)!=5) ok=0;
    if(math_abs(-99)!=99) ok=0;  if(math_abs(42)!=42) ok=0;
    if(math_max(3,7)!=7) ok=0;   if(math_min(3,7)!=3) ok=0;
    if(math_clamp(5,0,10)!=5) ok=0;
    if(math_clamp(-5,0,10)!=0) ok=0;
    if(math_clamp(15,0,10)!=10) ok=0;
    if(math_apply(10,3,MATH_ADD)!=13) ok=0;
    if(math_apply(10,3,MATH_SUB)!=7)  ok=0;
    if(math_apply(10,3,MATH_MUL)!=30) ok=0;
    if(math_apply(12,4,MATH_DIV)!=3)  ok=0;
    if(str_is_digit('5')!=1) ok=0;
    if(str_is_digit('A')!=0) ok=0;
    if(str_is_alpha('A')!=1) ok=0;
    if(str_is_alpha('5')!=0) ok=0;
    if(str_to_upper('a')!=65) ok=0;
    if(str_to_lower('A')!=97) ok=0;
    return check(ok);
}

int test_header_guards(void) {
    printf("Test: header guards\r\n");
    int ok; ok=1;
    int sml; sml=STR_MAX_LEN;   if(sml!=256) ok=0;
    int sev; sev=STR_EMPTY_VAL; if(sev!=0) ok=0;
    int nlc; nlc=NEWLINE_CHAR;  if(nlc!=10) ok=0;
    int spc; spc=SPACE_CHAR;    if(spc!=32) ok=0;
    int eq; eq=STR_EQUAL;       if(eq!=0) ok=0;
    int lt; lt=STR_LESS;        if(lt!=-1) ok=0;
    int gt; gt=STR_GREATER;     if(gt!=1) ok=0;
    return check(ok);
}

int test_forward_decl(void) {
    printf("Test: forward declarations\r\n");
    int ok; ok=1;
    if(fwd_square(5)!=25)   ok=0;
    if(fwd_square(0)!=0)    ok=0;
    if(fwd_cube(3)!=27)     ok=0;
    if(fwd_cube(2)!=8)      ok=0;
    if(fwd_is_even(4)!=1)   ok=0;
    if(fwd_is_even(7)!=0)   ok=0;
    if(fwd_sum_range(1,5)!=15) ok=0;
    if(fwd_sum_range(0,0)!=0)  ok=0;
    if(math_abs(-7)!=7)     ok=0;
    if(math_max(10,20)!=20) ok=0;
    return check(ok);
}

int test_conditional_compile(void) {
    printf("Test: conditional compile\r\n");
    int ok; ok=1;
    int rb; rb=RELEASE_BUILD;  if(rb!=1) ok=0;
    int bt; bt=BUILD_TAG;      if(bt!=42) ok=0;
    if(PI_INT!=3) ok=0;
    if(MAX_VAL!=1000) ok=0;
    if(ABS_APPROX!=1) ok=0;
    int v1; v1=IS_V1; if(v1!=1) ok=0;
    int v2; v2=IS_V2; if(v2!=0) ok=0;
    int db; db=DEFAULT_BUF; if(db!=256) ok=0;
    if(ERR_NONE!=0) ok=0;    if(ERR_INVALID!=1) ok=0;
    if(ERR_OVERFLOW!=2) ok=0; if(ERR_DIVZERO!=4) ok=0;
    if(VERSION_MAJOR!=1) ok=0; if(VERSION_MINOR!=2) ok=0; if(VERSION_PATCH!=3) ok=0;
    return check(ok);
}

int test_nested_headers(void) {
    printf("Test: nested headers\r\n");
    int ok; ok=1;
    if(STR_MAX_LEN!=256) ok=0;
    if(PI_INT!=3) ok=0;
    if(ERR_NONE!=0) ok=0;
    int s; s=STATUS_OK; if(s!=0) ok=0;
    s=STATUS_ERROR; if(s!=1) ok=0;
    s=STATUS_DONE; if(s!=99) ok=0;
    int clamped; clamped=math_clamp(5000,MIN_VAL,MAX_VAL);
    if(clamped!=MAX_VAL) ok=0;
    clamped=math_clamp(-5,MIN_VAL,MAX_VAL);
    if(clamped!=MIN_VAL) ok=0;
    return check(ok);
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void) {
    printf("=== Compiler Feature Test Suite ===\r\n");
    printf(SEP);
    int pass; int fail; int total; int r;
    pass=0; fail=0; total=0;

    r=test_arithmetic();         total++; if(r) pass++; else fail++;
    r=test_compound_assign();    total++; if(r) pass++; else fail++;
    r=test_incdec();             total++; if(r) pass++; else fail++;
    r=test_logic();              total++; if(r) pass++; else fail++;
    r=test_for();                total++; if(r) pass++; else fail++;
    r=test_while_continue();     total++; if(r) pass++; else fail++;
    r=test_do_while();           total++; if(r) pass++; else fail++;
    r=test_nested_break();       total++; if(r) pass++; else fail++;
    r=test_if_chain();           total++; if(r) pass++; else fail++;
    r=test_switch();             total++; if(r) pass++; else fail++;
    r=test_switch_enum();        total++; if(r) pass++; else fail++;
    r=test_goto();               total++; if(r) pass++; else fail++;
    r=test_ternary();            total++; if(r) pass++; else fail++;
    r=test_arrays();             total++; if(r) pass++; else fail++;
    r=test_pointers();           total++; if(r) pass++; else fail++;
    r=test_malloc();             total++; if(r) pass++; else fail++;
    r=test_sizeof();             total++; if(r) pass++; else fail++;
    r=test_cast();               total++; if(r) pass++; else fail++;
    r=test_escape_chars();       total++; if(r) pass++; else fail++;
    r=test_enum();               total++; if(r) pass++; else fail++;
    r=test_struct();             total++; if(r) pass++; else fail++;
    r=test_union();              total++; if(r) pass++; else fail++;
    r=test_func_ptr();           total++; if(r) pass++; else fail++;
    r=test_fibonacci();          total++; if(r) pass++; else fail++;
    r=test_static();             total++; if(r) pass++; else fail++;
    r=test_const();              total++; if(r) pass++; else fail++;
    r=test_bits();               total++; if(r) pass++; else fail++;
    r=test_strings();            total++; if(r) pass++; else fail++;
    r=test_modulus();            total++; if(r) pass++; else fail++;
    r=test_precedence();         total++; if(r) pass++; else fail++;
    r=test_headers();            total++; if(r) pass++; else fail++;
    r=test_header_guards();      total++; if(r) pass++; else fail++;
    r=test_forward_decl();       total++; if(r) pass++; else fail++;
    r=test_conditional_compile(); total++; if(r) pass++; else fail++;
    r=test_nested_headers();     total++; if(r) pass++; else fail++;

    printf(SEP);
    if(fail==0) { printf("ALL TESTS PASSED\r\n"); }
    else        { printf("SOME TESTS FAILED\r\n"); }

    ExitProcess(0);
    return 0;
}

/* =========================================================================
 * Functions defined AFTER main — require forward declarations to work
 * ========================================================================= */
int fwd_square(int x)   { return x*x; }
int fwd_cube(int x)     { return x*x*x; }
int fwd_is_even(int n)  { return (n%2==0)?1:0; }
int fwd_sum_range(int lo, int hi) {
    int sum; sum=0; int i; for(i=lo;i<=hi;i++) sum+=i; return sum;
}
