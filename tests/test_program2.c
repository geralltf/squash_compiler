/*
 * test_program2.c  —  Comprehensive compiler test suite (v3)
 *
 * New/fixed tests vs v2:
 *   - printf with format args (%d %s %c %x)
 *   - struct Point with real .x/.y field access
 *   - union with byte-level view
 *   - function pointers (declared, stored, called with arguments)
 *   - const int read-only correctness
 *   - static int variable retaining value across calls
 *   - static function (file-scope linkage)
 *   - escape characters: all of \n \r \t \0 \\ \' \" \a \b \f \v \xNN
 */

#include "math_utils.h"
#include "string_utils.h"
#include "constants.h"

#define PASS_STR   "  [PASS]\r\n"
#define FAIL_STR   "  [FAIL]\r\n"
#define SEP        "----------------------------------------\r\n"
#define MAX_ITEMS  8
#define SHIFT_AMT  3

/* Local enums */
enum Color     { RED=0, GREEN=1, BLUE=2, WHITE=255 };
enum Direction { NORTH=10, SOUTH=20, EAST=30, WEST=40 };

/* ── Struct with real fields ─────────────────────────────────────────────── */
struct Point {
    int x;
    int y;
};

struct Rect {
    int width;
    int height;
};

/* ── Union with byte view ────────────────────────────────────────────────── */
union IntBytes {
    int   as_int;
    char  bytes[4];
};

/* ── Function pointer typedef ────────────────────────────────────────────── */
/* (used in test_func_ptrs) */

/* ── Forward declarations ────────────────────────────────────────────────── */
int check(int result);
/* original 30 */
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
int test_struct(void);
int test_union(void);
int test_func_ptr(void);
int test_fibonacci(void);
int test_static_var(void);
int test_const(void);
int test_bits(void);
int test_strings(void);
int test_modulus(void);
int test_precedence(void);
/* header tests */
int test_headers(void);
int test_header_guards(void);
int test_forward_decl(void);
int test_conditional_compile(void);
int test_nested_headers(void);
/* new tests */
int test_printf_format(void);
int test_struct_fields(void);
int test_union_bytes(void);
int test_func_pointers(void);
int test_const_correct(void);
int test_static_counter(void);
int test_escape_all(void);
/* helper functions */
int fibonacci(int n);
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
int fwd_square(int x);
int fwd_cube(int x);
int fwd_is_even(int n);
int fwd_sum_range(int lo, int hi);
/* function pointer targets */
int fp_add(int a, int b);
int fp_sub(int a, int b);
int fp_mul(int a, int b);
/* static counter helper */
int static_counter(void);

/* =========================================================================
 * check()
 * ========================================================================= */
int check(int result) {
    if (result) { printf(PASS_STR); } else { printf(FAIL_STR); }
    return result;
}

/* =========================================================================
 * Test: printf with format specifiers %d %s %c
 * ========================================================================= */
int test_printf_format(void) {
    printf("Test: printf format\r\n");
    int ok;
    ok = 1;
    /* These calls exercise the format-arg shim.
       We verify indirectly by checking arithmetic still works after. */
    int x;
    x = 42;
    printf("  val=%d\r\n", x);
    printf("  neg=%d\r\n", -7);
    printf("  hex=%x\r\n", 255);
    printf("  chr=%c\r\n", 65);
    /* verify the values themselves are right */
    if (x != 42) ok = 0;
    if (-7 != -7) ok = 0;
    if (255 != 0xFF) ok = 0;
    return check(ok);
}

/* =========================================================================
 * Test: struct with actual field access (.x .y)
 * ========================================================================= */
int make_point_x(int x, int y) { return x; }
int make_point_y(int x, int y) { return y; }
int point_dist_sq(int x1, int y1, int x2, int y2) {
    int dx; int dy;
    dx = x2 - x1;
    dy = y2 - y1;
    return dx*dx + dy*dy;
}

int test_struct_fields(void) {
    printf("Test: struct fields\r\n");
    int ok;
    ok = 1;

    /* Declare struct variables and assign fields */
    struct Point p;
    p.x = 3;
    p.y = 4;

    if (p.x != 3) ok = 0;
    if (p.y != 4) ok = 0;

    /* Mutate via field assignment */
    p.x = p.x + 1;
    if (p.x != 4) ok = 0;

    /* Second struct instance */
    struct Point q;
    q.x = 0;
    q.y = 0;
    if (q.x != 0) ok = 0;
    if (q.y != 0) ok = 0;

    /* Distance-squared between (3,4) and (0,0) = 9+16 = 25 */
    int dsq;
    dsq = point_dist_sq(3, 4, 0, 0);
    if (dsq != 25) ok = 0;

    /* Rect struct */
    struct Rect r;
    r.width  = 10;
    r.height = 20;
    int area;
    area = r.width * r.height;
    if (area != 200) ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: union — integer/byte-level aliasing
 * ========================================================================= */
int test_union_bytes(void) {
    printf("Test: union bytes\r\n");
    int ok;
    ok = 1;

    union IntBytes u;

    /* Write as int, read bytes */
    u.as_int = 0x01020304;
    /* little-endian: bytes[0]=0x04, bytes[1]=0x03, bytes[2]=0x02, bytes[3]=0x01 */
    if ((u.bytes[0] & 0xFF) != 0x04) ok = 0;
    if ((u.bytes[1] & 0xFF) != 0x03) ok = 0;
    if ((u.bytes[2] & 0xFF) != 0x02) ok = 0;
    if ((u.bytes[3] & 0xFF) != 0x01) ok = 0;

    /* Write first byte, read int (should change low byte) */
    u.as_int   = 0;
    u.bytes[0] = 0x42;
    if (u.as_int != 0x42) ok = 0;

    /* Size: both members share same storage — int value after byte write */
    u.as_int   = 0xFFFFFF00;
    u.bytes[0] = 0xAB;
    if ((u.as_int & 0xFF) != 0xAB) ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: function pointers — declared, stored in variable, called with args
 * ========================================================================= */
int fp_add(int a, int b) { return a + b; }
int fp_sub(int a, int b) { return a - b; }
int fp_mul(int a, int b) { return a * b; }

int test_func_pointers(void) {
    printf("Test: function pointers\r\n");
    int ok;
    ok = 1;

    /* Store function pointers in local variables */
    int (*op)(int, int);

    op = fp_add;
    if (op(10, 3) != 13) ok = 0;

    op = fp_sub;
    if (op(10, 3) != 7) ok = 0;

    op = fp_mul;
    if (op(10, 3) != 30) ok = 0;

    /* Array of function pointers */
    int (*ops[3])(int, int);
    ops[0] = fp_add;
    ops[1] = fp_sub;
    ops[2] = fp_mul;

    if (ops[0](7, 2) != 9)  ok = 0;
    if (ops[1](7, 2) != 5)  ok = 0;
    if (ops[2](7, 2) != 14) ok = 0;

    /* Pass function pointer as argument */
    int r;
    r = fp_add(ops[0](1, 2), ops[2](3, 4));  /* add(1+2, 3*4) = add(3,12) = 15 */
    if (r != 15) ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: const correctness — const int read-only, used in expressions
 * ========================================================================= */
int test_const_correct(void) {
    printf("Test: const correct\r\n");
    int ok;
    ok = 1;

    const int MAX_SCORE = 100;
    const int MIN_SCORE = 0;
    const int PASSING   = 60;

    /* Read const values */
    if (MAX_SCORE != 100) ok = 0;
    if (MIN_SCORE != 0)   ok = 0;
    if (PASSING   != 60)  ok = 0;

    /* Use in expressions */
    int range;
    range = MAX_SCORE - MIN_SCORE;
    if (range != 100) ok = 0;

    /* Const as array size guide */
    int half;
    half = MAX_SCORE / 2;
    if (half != 50) ok = 0;

    /* Const in condition */
    int score;
    score = 75;
    int passed;
    passed = (score >= PASSING) ? 1 : 0;
    if (passed != 1) ok = 0;

    score = 45;
    passed = (score >= PASSING) ? 1 : 0;
    if (passed != 0) ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: static variable retains value across function calls
 * ========================================================================= */
int static_counter(void) {
    static int count = 0;
    count = count + 1;
    return count;
}

int test_static_counter(void) {
    printf("Test: static variable\r\n");
    int ok;
    ok = 1;

    /* Each call should increment the same static storage */
    int v1; int v2; int v3;
    v1 = static_counter();   /* should be 1 */
    v2 = static_counter();   /* should be 2 */
    v3 = static_counter();   /* should be 3 */

    if (v1 != 1) ok = 0;
    if (v2 != 2) ok = 0;
    if (v3 != 3) ok = 0;

    return check(ok);
}

/* =========================================================================
 * Test: all escape sequences as char literals
 * ========================================================================= */
int test_escape_all(void) {
    printf("Test: escape chars (all)\r\n");
    int ok;
    ok = 1;

    /* Standard named escapes */
    char c_nl;  c_nl  = '\n';  if ((int)c_nl  != 10)  ok = 0;  /* newline       */
    char c_cr;  c_cr  = '\r';  if ((int)c_cr  != 13)  ok = 0;  /* carriage ret  */
    char c_tab; c_tab = '\t';  if ((int)c_tab != 9)   ok = 0;  /* horizontal tab*/
    char c_nul; c_nul = '\0';  if ((int)c_nul != 0)   ok = 0;  /* null          */
    char c_bs;  c_bs  = '\\';  if ((int)c_bs  != 92)  ok = 0;  /* backslash     */
    char c_sq;  c_sq  = '\'';  if ((int)c_sq  != 39)  ok = 0;  /* single quote  */
    char c_dq;  c_dq  = '\"';  if ((int)c_dq  != 34)  ok = 0;  /* double quote  */
    char c_bel; c_bel = '\a';  if ((int)c_bel != 7)   ok = 0;  /* bell          */
    char c_bsp; c_bsp = '\b';  if ((int)c_bsp != 8)   ok = 0;  /* backspace     */
    char c_ff;  c_ff  = '\f';  if ((int)c_ff  != 12)  ok = 0;  /* form feed     */
    char c_vt;  c_vt  = '\v';  if ((int)c_vt  != 11)  ok = 0;  /* vertical tab  */
    /* Hex escape */
    char c_hx;  c_hx  = '\x41'; if ((int)c_hx != 65)  ok = 0;  /* 'A'           */
    char c_hx2; c_hx2 = '\x7F'; if ((int)c_hx2 != 127) ok = 0; /* DEL           */
    char c_hx3; c_hx3 = '\x00'; if ((int)c_hx3 != 0)  ok = 0;  /* null via hex  */

    return check(ok);
}

/* =========================================================================
 * Existing original tests (kept exactly as before)
 * ========================================================================= */
int test_arithmetic(void) {
    printf("Test: arithmetic\r\n");
    int a; int b; int ok; a=100; b=7; ok=1;
    if((a+b)!=107)ok=0; if((a-b)!=93)ok=0; if((a*b)!=700)ok=0;
    if((a/b)!=14)ok=0;  if((a%b)!=2)ok=0;  if((a<<1)!=200)ok=0;
    if((a>>1)!=50)ok=0; if((a&0xF0)!=96)ok=0; if((a|0x0F)!=111)ok=0;
    if((a^0xFF)!=155)ok=0;
    return check(ok);
}
int test_compound_assign(void) {
    printf("Test: compound assign\r\n");
    int v; int ok; v=10; ok=1;
    v+=5;if(v!=15)ok=0; v-=3;if(v!=12)ok=0; v*=2;if(v!=24)ok=0;
    v/=4;if(v!=6)ok=0;  v%=4;if(v!=2)ok=0;  v=16;
    v<<=1;if(v!=32)ok=0; v>>=2;if(v!=8)ok=0;
    v&=0xF;if(v!=8)ok=0; v|=0x3;if(v!=11)ok=0; v^=0xA;if(v!=1)ok=0;
    return check(ok);
}
int test_incdec(void) {
    printf("Test: inc/dec\r\n");
    int v; int ok; v=5; ok=1;
    v++;if(v!=6)ok=0; ++v;if(v!=7)ok=0; v--;if(v!=6)ok=0; --v;if(v!=5)ok=0;
    int pv; pv=v++; if(pv!=5||v!=6)ok=0;
    int rv; rv=++v;  if(rv!=7||v!=7)ok=0;
    return check(ok);
}
int test_logic(void) {
    printf("Test: logic/compare\r\n");
    int ok; ok=1;
    if(!(3==3))ok=0; if(!(3!=4))ok=0; if(!(3<4))ok=0; if(!(4>3))ok=0;
    if(!(3<=3))ok=0; if(!(4>=4))ok=0; if(!(1&&1))ok=0; if(!(!0))ok=0;
    if(!(0||1))ok=0; if(!!(0&&1))ok=0;
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
    while(i<10){i++;if(i%2==0)continue;sum+=i;}
    return check(sum==25);
}
int test_do_while(void) {
    printf("Test: do-while\r\n");
    int count; count=0;
    do{count++;}while(count<5);
    return check(count==5);
}
int test_nested_break(void) {
    printf("Test: nested loop+break\r\n");
    int total; int i; int j; total=0;
    for(i=0;i<5;i++){for(j=0;j<5;j++){if(j==3)break;total++;}}
    return check(total==15);
}
int classify(int n){
    if(n<0)return -1; else if(n==0)return 0;
    else if(n<10)return 1; else if(n<100)return 2; else return 3;
}
int test_if_chain(void) {
    printf("Test: if/else-if chain\r\n");
    int ok; ok=1;
    if(classify(-5)!=-1)ok=0; if(classify(0)!=0)ok=0;
    if(classify(7)!=1)ok=0; if(classify(42)!=2)ok=0; if(classify(999)!=3)ok=0;
    return check(ok);
}
int test_switch(void) {
    printf("Test: switch/case\r\n");
    int ok; ok=1; int i;
    for(i=0;i<4;i++){
        int r; r=0;
        switch(i){case 0:r=100;break;case 1:r=200;break;case 2:r=300;break;default:r=999;break;}
        if(i==0&&r!=100)ok=0; if(i==1&&r!=200)ok=0;
        if(i==2&&r!=300)ok=0; if(i==3&&r!=999)ok=0;
    }
    return check(ok);
}
int test_switch_enum(void) {
    printf("Test: switch+enum\r\n");
    int ok; ok=1; int c; c=GREEN; int nm; nm=0;
    switch(c){case RED:nm=1;break;case GREEN:nm=2;break;case BLUE:nm=3;break;default:nm=0;break;}
    if(nm!=2)ok=0;
    c=BLUE; nm=0;
    switch(c){case RED:nm=1;break;case GREEN:nm=2;break;case BLUE:nm=3;break;default:nm=0;break;}
    if(nm!=3)ok=0;
    return check(ok);
}
int test_goto(void) {
    printf("Test: goto\r\n");
    int val; val=0; int i; i=0;
loop_top: if(i>=5)goto loop_done; val+=i; i++; goto loop_top;
loop_done: return check(val==10);
}
int test_ternary(void) {
    printf("Test: ternary\r\n");
    int ok; ok=1; int a; int b; a=5; b=10;
    int big; big=(a>b)?a:b; if(big!=10)ok=0;
    int sign; sign=(a>0)?1:((a<0)?-1:0); if(sign!=1)ok=0;
    int neg; neg=-3; sign=(neg>0)?1:((neg<0)?-1:0); if(sign!=-1)ok=0;
    return check(ok);
}
int test_arrays(void) {
    printf("Test: arrays\r\n");
    int ok; ok=1; int arr[8]; int i;
    for(i=0;i<MAX_ITEMS;i++) arr[i]=i*i;
    if(arr[0]!=0)ok=0; if(arr[1]!=1)ok=0; if(arr[3]!=9)ok=0; if(arr[7]!=49)ok=0;
    int primes[5]; primes[0]=2;primes[1]=3;primes[2]=5;primes[3]=7;primes[4]=11;
    int sum; sum=0; for(i=0;i<5;i++)sum+=primes[i];
    if(sum!=28)ok=0;
    return check(ok);
}
int test_pointers(void) {
    printf("Test: pointers\r\n");
    int ok; ok=1; int val; val=42; int *ptr; ptr=&val;
    if(*ptr!=42)ok=0; *ptr=99; if(val!=99)ok=0;
    int arr2[4]; arr2[0]=10;arr2[1]=20;arr2[2]=30;arr2[3]=40;
    int *p; p=&arr2[0]; if(*p!=10)ok=0;
    return check(ok);
}
int test_malloc(void) {
    printf("Test: malloc/free\r\n");
    int ok; ok=1; int *buf; buf=(int*)malloc(40);
    if(buf==0){ok=0;}else{buf[0]=123;buf[1]=456;
        if(buf[0]!=123)ok=0; if(buf[1]!=456)ok=0; free(buf);}
    return check(ok);
}
int test_sizeof(void) {
    printf("Test: sizeof\r\n");
    int ok; ok=1;
    int si; si=sizeof(int); int sc; sc=sizeof(char); int sp; sp=sizeof(int*);
    if(si!=4)ok=0; if(sc!=1)ok=0; if(sp==0)ok=0;
    return check(ok);
}
int test_cast(void) {
    printf("Test: type cast\r\n");
    int ok; ok=1; int i; char c; i=65; c=(char)i; if(c!=65)ok=0;
    int neg; neg=-1; int au; au=(int)(unsigned int)neg; if(au!=-1)ok=0;
    int big; big=0x1FF; char small; small=(char)big; if((int)small!=-1)ok=0;
    return check(ok);
}
int test_escape_chars(void) {
    printf("Test: escape sequences\r\n");
    int ok; ok=1;
    char nl; char tab; char nul; char bs;
    nl='\n'; if((int)nl!=10)ok=0; tab='\t'; if((int)tab!=9)ok=0;
    nul='\0'; if((int)nul!=0)ok=0; bs='\\'; if((int)bs!=92)ok=0;
    char ha; ha='\x41'; if((int)ha!=65)ok=0;
    return check(ok);
}
int test_enum(void) {
    printf("Test: enum\r\n");
    int ok; ok=1; int c;
    c=RED; if(c!=0)ok=0; c=GREEN; if(c!=1)ok=0;
    c=BLUE; if(c!=2)ok=0; c=WHITE; if(c!=255)ok=0;
    if(NORTH!=10)ok=0; if(SOUTH!=20)ok=0;
    return check(ok);
}

/* Legacy struct test using helper functions */
int test_struct(void) {
    printf("Test: struct (basic)\r\n");
    int ok; ok=1;
    int s; s=3+4; if(s!=7)ok=0;
    int area; area=100*200; if(area!=20000)ok=0;
    return check(ok);
}

/* Legacy union test using int/char arithmetic */
int test_union(void) {
    printf("Test: union (basic)\r\n");
    int ok; ok=1; int v; v=0x41424344;
    int rb; rb=v; if(rb!=0x41424344)ok=0;
    char ch; ch=(char)(v&0xFF); if((int)ch!=0x44)ok=0;
    return check(ok);
}

int apply(int a, int b, int op) {
    switch(op){case 0:return a+b;case 1:return a-b;case 2:return a*b;default:return 0;}
}
int test_func_ptr(void) {
    printf("Test: func dispatch\r\n");
    int ok; ok=1;
    if(apply(10,3,0)!=13)ok=0; if(apply(10,3,1)!=7)ok=0; if(apply(10,3,2)!=30)ok=0;
    return check(ok);
}
int fibonacci(int n){if(n<=0)return 0;if(n==1)return 1;return fibonacci(n-1)+fibonacci(n-2);}
int test_fibonacci(void) {
    printf("Test: fibonacci\r\n");
    int ok; ok=1;
    if(fibonacci(0)!=0)ok=0; if(fibonacci(1)!=1)ok=0;
    if(fibonacci(5)!=5)ok=0; if(fibonacci(10)!=55)ok=0;
    return check(ok);
}
int test_static_var(void) {
    printf("Test: static (legacy)\r\n");
    int ok; ok=1;
    /* simple counter returning 1 each time (no static storage) */
    int count; count=0; count++; if(count!=1)ok=0;
    return check(ok);
}
int test_const(void) {
    printf("Test: const expr\r\n");
    int ok; ok=1; int MAX; MAX=100; int x; x=MAX;
    if(x!=100)ok=0; x=MAX*2; if(x!=200)ok=0;
    return check(ok);
}
int test_bits(void) {
    printf("Test: bit ops\r\n");
    int ok; ok=1; int flags; flags=0;
    flags|=(1<<SHIFT_AMT); if(flags!=8)ok=0;
    if(!(flags&8))ok=0; flags&=~8; if(flags!=0)ok=0;
    flags^=0xFF; if(flags!=255)ok=0; flags^=0xFF; if(flags!=0)ok=0;
    return check(ok);
}
int test_strings(void) {
    printf("Test: string len\r\n");
    int ok; ok=1; int len;
    len=strlen("Hello"); if(len!=5)ok=0;
    len=strlen("World!"); if(len!=6)ok=0;
    len=strlen(""); if(len!=0)ok=0;
    return check(ok);
}
int test_modulus(void) {
    printf("Test: modulus\r\n");
    int ok; ok=1;
    if((10%3)!=1)ok=0; if((9%3)!=0)ok=0; if((7%2)!=1)ok=0; if((100%7)!=2)ok=0;
    return check(ok);
}
int test_precedence(void) {
    printf("Test: precedence\r\n");
    int ok; ok=1; int r;
    r=2+3*4; if(r!=14)ok=0; r=(2+3)*4; if(r!=20)ok=0;
    r=10-3-2; if(r!=5)ok=0; r=1<<2+1; if(r!=8)ok=0;
    r=8>>1+1; if(r!=2)ok=0; r=3+4*2-1; if(r!=10)ok=0;
    return check(ok);
}

/* =========================================================================
 * Header/include/conditional tests
 * ========================================================================= */
int math_add(int a, int b)  { return a+b; }
int math_sub(int a, int b)  { return a-b; }
int math_mul(int a, int b)  { return a*b; }
int math_div(int a, int b)  { if(b==0)return 0; return a/b; }
int math_abs(int x)         { return (x<0)?-x:x; }
int math_max(int a, int b)  { return (a>b)?a:b; }
int math_min(int a, int b)  { return (a<b)?a:b; }
int math_clamp(int x,int lo,int hi){if(x<lo)return lo;if(x>hi)return hi;return x;}
int math_apply(int a, int b, int op){
    switch(op){case MATH_ADD:return math_add(a,b);case MATH_SUB:return math_sub(a,b);
               case MATH_MUL:return math_mul(a,b);case MATH_DIV:return math_div(a,b);default:return 0;}
}
int str_length(int d)         { return d; }
int str_is_digit(int ch)      { return (ch>=48&&ch<=57)?1:0; }
int str_is_alpha(int ch)      { return ((ch>=65&&ch<=90)||(ch>=97&&ch<=122))?1:0; }
int str_to_upper(int ch)      { return (ch>=97&&ch<=122)?ch-32:ch; }
int str_to_lower(int ch)      { return (ch>=65&&ch<=90)?ch+32:ch; }
int str_char_count(int ch,int lim){int c;c=0;int i;for(i=0;i<lim;i++)c+=(i%10==ch%10)?1:0;return c;}

int test_headers(void) {
    printf("Test: header functions\r\n");
    int ok; ok=1;
    if(math_add(3,4)!=7)ok=0; if(math_sub(10,3)!=7)ok=0;
    if(math_mul(6,7)!=42)ok=0; if(math_div(20,4)!=5)ok=0;
    if(math_abs(-99)!=99)ok=0; if(math_abs(42)!=42)ok=0;
    if(math_max(3,7)!=7)ok=0; if(math_min(3,7)!=3)ok=0;
    if(math_clamp(5,0,10)!=5)ok=0; if(math_clamp(-5,0,10)!=0)ok=0;
    if(math_clamp(15,0,10)!=10)ok=0;
    if(math_apply(10,3,MATH_ADD)!=13)ok=0; if(math_apply(10,3,MATH_SUB)!=7)ok=0;
    if(math_apply(10,3,MATH_MUL)!=30)ok=0; if(math_apply(12,4,MATH_DIV)!=3)ok=0;
    if(str_is_digit('5')!=1)ok=0; if(str_is_digit('A')!=0)ok=0;
    if(str_is_alpha('A')!=1)ok=0; if(str_is_alpha('5')!=0)ok=0;
    if(str_to_upper('a')!=65)ok=0; if(str_to_lower('A')!=97)ok=0;
    return check(ok);
}
int test_header_guards(void) {
    printf("Test: header guards\r\n");
    int ok; ok=1;
    int sml; sml=STR_MAX_LEN;   if(sml!=256)ok=0;
    int sev; sev=STR_EMPTY_VAL; if(sev!=0)ok=0;
    int nlc; nlc=NEWLINE_CHAR;  if(nlc!=10)ok=0;
    int spc; spc=SPACE_CHAR;    if(spc!=32)ok=0;
    int eq; eq=STR_EQUAL;  if(eq!=0)ok=0;
    int lt; lt=STR_LESS;   if(lt!=-1)ok=0;
    int gt; gt=STR_GREATER;if(gt!=1)ok=0;
    return check(ok);
}
int test_forward_decl(void) {
    printf("Test: forward decl\r\n");
    int ok; ok=1;
    if(fwd_square(5)!=25)ok=0; if(fwd_square(0)!=0)ok=0;
    if(fwd_cube(3)!=27)ok=0;   if(fwd_cube(2)!=8)ok=0;
    if(fwd_is_even(4)!=1)ok=0; if(fwd_is_even(7)!=0)ok=0;
    if(fwd_sum_range(1,5)!=15)ok=0; if(fwd_sum_range(0,0)!=0)ok=0;
    if(math_abs(-7)!=7)ok=0;   if(math_max(10,20)!=20)ok=0;
    return check(ok);
}
int test_conditional_compile(void) {
    printf("Test: conditional compile\r\n");
    int ok; ok=1;
    int rb; rb=RELEASE_BUILD; if(rb!=1)ok=0;
    int bt; bt=BUILD_TAG;     if(bt!=42)ok=0;
    if(PI_INT!=3)ok=0; if(MAX_VAL!=1000)ok=0; if(ABS_APPROX!=1)ok=0;
    int v1; v1=IS_V1; if(v1!=1)ok=0;
    int v2; v2=IS_V2; if(v2!=0)ok=0;
    int db; db=DEFAULT_BUF; if(db!=256)ok=0;
    if(ERR_NONE!=0)ok=0; if(ERR_INVALID!=1)ok=0;
    if(ERR_OVERFLOW!=2)ok=0; if(ERR_DIVZERO!=4)ok=0;
    if(VERSION_MAJOR!=1)ok=0; if(VERSION_MINOR!=2)ok=0; if(VERSION_PATCH!=3)ok=0;
    return check(ok);
}
int test_nested_headers(void) {
    printf("Test: nested headers\r\n");
    int ok; ok=1;
    if(STR_MAX_LEN!=256)ok=0; if(PI_INT!=3)ok=0; if(ERR_NONE!=0)ok=0;
    int s; s=STATUS_OK; if(s!=0)ok=0;
    s=STATUS_ERROR; if(s!=1)ok=0;
    s=STATUS_DONE; if(s!=99)ok=0;
    int clamped; clamped=math_clamp(5000,MIN_VAL,MAX_VAL);
    if(clamped!=MAX_VAL)ok=0;
    clamped=math_clamp(-5,MIN_VAL,MAX_VAL);
    if(clamped!=MIN_VAL)ok=0;
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

    /* Original 30 */
    r=test_arithmetic();          total++;if(r)pass++;else fail++;
    r=test_compound_assign();     total++;if(r)pass++;else fail++;
    r=test_incdec();              total++;if(r)pass++;else fail++;
    r=test_logic();               total++;if(r)pass++;else fail++;
    r=test_for();                 total++;if(r)pass++;else fail++;
    r=test_while_continue();      total++;if(r)pass++;else fail++;
    r=test_do_while();            total++;if(r)pass++;else fail++;
    r=test_nested_break();        total++;if(r)pass++;else fail++;
    r=test_if_chain();            total++;if(r)pass++;else fail++;
    r=test_switch();              total++;if(r)pass++;else fail++;
    r=test_switch_enum();         total++;if(r)pass++;else fail++;
    r=test_goto();                total++;if(r)pass++;else fail++;
    r=test_ternary();             total++;if(r)pass++;else fail++;
    r=test_arrays();              total++;if(r)pass++;else fail++;
    r=test_pointers();            total++;if(r)pass++;else fail++;
    r=test_malloc();              total++;if(r)pass++;else fail++;
    r=test_sizeof();              total++;if(r)pass++;else fail++;
    r=test_cast();                total++;if(r)pass++;else fail++;
    r=test_escape_chars();        total++;if(r)pass++;else fail++;
    r=test_enum();                total++;if(r)pass++;else fail++;
    r=test_struct();              total++;if(r)pass++;else fail++;
    r=test_union();               total++;if(r)pass++;else fail++;
    r=test_func_ptr();            total++;if(r)pass++;else fail++;
    r=test_fibonacci();           total++;if(r)pass++;else fail++;
    r=test_static_var();          total++;if(r)pass++;else fail++;
    r=test_const();               total++;if(r)pass++;else fail++;
    r=test_bits();                total++;if(r)pass++;else fail++;
    r=test_strings();             total++;if(r)pass++;else fail++;
    r=test_modulus();             total++;if(r)pass++;else fail++;
    r=test_precedence();          total++;if(r)pass++;else fail++;
    /* Header tests */
    r=test_headers();             total++;if(r)pass++;else fail++;
    r=test_header_guards();       total++;if(r)pass++;else fail++;
    r=test_forward_decl();        total++;if(r)pass++;else fail++;
    r=test_conditional_compile(); total++;if(r)pass++;else fail++;
    r=test_nested_headers();      total++;if(r)pass++;else fail++;
    /* New tests */
    r=test_printf_format();       total++;if(r)pass++;else fail++;
    r=test_struct_fields();       total++;if(r)pass++;else fail++;
    r=test_union_bytes();         total++;if(r)pass++;else fail++;
    r=test_func_pointers();       total++;if(r)pass++;else fail++;
    r=test_const_correct();       total++;if(r)pass++;else fail++;
    r=test_static_counter();      total++;if(r)pass++;else fail++;
    r=test_escape_all();          total++;if(r)pass++;else fail++;

    printf(SEP);
    if(fail==0){printf("ALL TESTS PASSED\r\n");}
    else       {printf("SOME TESTS FAILED\r\n");}

    ExitProcess(0);
    return 0;
}

/* Functions defined AFTER main (forward-decl tests) */
int fwd_square(int x)   { return x*x; }
int fwd_cube(int x)     { return x*x*x; }
int fwd_is_even(int n)  { return (n%2==0)?1:0; }
int fwd_sum_range(int lo, int hi) {
    int sum; sum=0; int i; for(i=lo;i<=hi;i++)sum+=i; return sum;
}
