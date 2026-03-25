/*
 * test_features.c - Comprehensive compiler feature test
 * Covers: bool, int types, char types, float/double, string.h,
 *         ctype.h, stdlib.h, typedef, inline, extern, static,
 *         auto, nested structs, linked lists, void*, bitwise,
 *         forward decls, union, main(int argc, char *argv[])
 */
#include "include/stdbool.h"
#include "include/stdint.h"
#include "include/stdlib.h"
#include "include/string.h"
#include "include/ctype.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(int ok, const char *name) {
    if (ok) { printf("PASS %s\r\n", name); g_pass = g_pass + 1; }
    else     { printf("FAIL %s\r\n", name); g_fail = g_fail + 1; }
}

/* typedefs */
typedef int          myint;
typedef double       myfloat;
typedef char*        mystr;
typedef struct { int x; int y; } Vec2;
typedef Vec2*        pVec2;
typedef unsigned int uint;

/* static + inline functions */
static int  static_add(int a, int b)    { return a + b; }
static inline int inline_sq(int x)      { return x * x; }
static inline double inline_dsq(double x){ return x * x; }

/* extern declaration */
extern void ExitProcess(unsigned int code);

/* nested struct */
struct Inner { int val; int pad; };
struct Outer { struct Inner a; struct Inner b; int sum; };

/* self-referential struct for linked list */
struct Node { int data; struct Node *next; };

/* union inside struct */
struct Variant { int type; union { int ival; float fval; } as; };

/* forward declaration */
int fwd_double(int x);

/* ======== tests ======== */

void test_bool(void) {
    bool t = true;
    bool f = false;
    check(t == 1,       "bool:true==1");
    check(f == 0,       "bool:false==0");
    check(t && !f,      "bool:&&!");
    bool b = (5 > 3);
    check(b,            "bool:from_cmp");
}

void test_int_types(void) {
    myint   a  = 42;
    int     b  = -100;
    long    l  = 123456L;
    uint    u  = 3000000000U;
    int8_t  i8 = -127;
    uint8_t u8 = 255;
    int32_t  i32 = -2000000;
    uint32_t u32 = 4000000000U;
    check(a   == 42,           "int:typedef");
    check(b   == -100,         "int:negative");
    check(l   == 123456,       "int:long");
    check(u   == 3000000000U,  "int:unsigned");
    check(i8  == -127,         "int:int8_t");
    check(u8  == 255,          "int:uint8_t");
    check(i32 == -2000000,     "int:int32_t");
    check(u32 == 4000000000U,  "int:uint32_t");
}

void test_char_types(void) {
    char          c  = 'A';
    signed char   sc = -50;
    unsigned char uc = 200;
    check(c  == 65,        "char:A==65");
    check(sc == -50,       "char:signed");
    check(uc == 200,       "char:unsigned");
    check(c + 1 == 'B',   "char:arith");
}

void test_float(void) {
    double pi = 3.14159;
    double e  = 2.71828;
    myfloat md = 2.5;

    check((int)((pi+e)*100) == 585,  "float:add");
    check((int)((pi-e)*100) == 42,   "float:sub");
    check((int)(pi*2.0*100) == 628,  "float:mul");
    check((int)(pi/2.0*100) == 157,  "float:div");
    check(pi > e,    "float:gt");
    check(e  < pi,   "float:lt");
    check(pi != e,   "float:ne");
    check(pi == pi,  "float:eq");

    double neg = -pi;
    check((int)(neg * -100) == 314,  "float:neg");

    int iv = 7;
    double di = (double)iv;
    check((int)(di * 10.0) == 70,    "float:int2dbl");

    double dv = 3.9;
    int    truncd = (int)dv;
    check(truncd == 3,               "float:dbl2int");
    check((int)(md*10)==25,          "float:typedef");

    double s = 1.5 + 2.5;
    check((int)s == 4,               "float:literal_add");
}

void test_string_functions(void) {
    char buf[64];
    strcpy(buf, "hello");
    check(strlen(buf) == 5,             "str:strlen");
    check(strcmp(buf,"hello")==0,       "str:strcmp_eq");
    check(strcmp(buf,"world")<0,        "str:strcmp_lt");
    check(strcmp("z","a")>0,            "str:strcmp_gt");

    char b2[64];
    strncpy(b2,"abcdef",3); b2[3]=0;
    check(strcmp(b2,"abc")==0,          "str:strncpy");

    strcat(buf," world");
    check(strcmp(buf,"hello world")==0, "str:strcat");

    char *p = strchr(buf,'o');
    check(p!=NULL && *p=='o',           "str:strchr");

    char *q = strstr(buf,"world");
    check(q!=NULL,                      "str:strstr_found");
    check(strcmp(q,"world")==0,         "str:strstr_match");

    char mb[16];
    memset(mb,'X',5); mb[5]=0;
    check(mb[0]=='X'&&mb[4]=='X',       "str:memset");

    int src[4]; src[0]=1;src[1]=2;src[2]=3;src[3]=4;
    int dst[4];
    memcpy(dst,src,16);
    check(dst[0]==1&&dst[3]==4,         "str:memcpy");
    check(memcmp("abc","abc",3)==0,     "str:memcmp_eq");
    check(memcmp("abc","abd",3)<0,      "str:memcmp_lt");
}

void test_ctype(void) {
    check(isdigit('5'),          "ctype:isdigit");
    check(!isdigit('a'),         "ctype:!isdigit");
    check(isalpha('z'),          "ctype:isalpha");
    check(!isalpha('1'),         "ctype:!isalpha");
    check(isalnum('a'),          "ctype:isalnum_a");
    check(isalnum('5'),          "ctype:isalnum_5");
    check(isspace(' '),          "ctype:isspace");
    check(!isspace('x'),         "ctype:!isspace");
    check(toupper('a')=='A',     "ctype:toupper");
    check(tolower('Z')=='z',     "ctype:tolower");
    check(isprint('!'),          "ctype:isprint");
    check(!isprint('\x01'),      "ctype:!isprint");
}

void test_stdlib(void) {
    check(atoi("42")==42,             "stdlib:atoi_pos");
    check(atoi("-99")==-99,           "stdlib:atoi_neg");
    check(atoi("  7")==7,             "stdlib:atoi_spaces");
    double d = atof("3.14");
    check((int)(d*100)==314,          "stdlib:atof");
    check(abs(-5)==5,                 "stdlib:abs_neg");
    check(abs(5)==5,                  "stdlib:abs_pos");

    int *p=(int*)malloc(16);
    check(p!=NULL,                    "stdlib:malloc_nonnull");
    p[0]=10; p[3]=40;
    check(p[0]==10&&p[3]==40,         "stdlib:malloc_rw");
    free(p);

    int *q=(int*)calloc(4,4);
    check(q!=NULL,                    "stdlib:calloc_nonnull");
    check(q[0]==0&&q[3]==0,           "stdlib:calloc_zero");
    q[2]=99;
    check(q[2]==99,                   "stdlib:calloc_write");
    free(q);
}

void test_typedef(void) {
    myint  a=100;
    myfloat b=1.5;
    mystr  s="hello";
    check(a==100,                     "typedef:int");
    check((int)(b*10)==15,            "typedef:double");
    check(strcmp(s,"hello")==0,       "typedef:char*");
    Vec2 v; v.x=3; v.y=4;
    check(v.x==3&&v.y==4,            "typedef:struct");
    pVec2 pv=&v;
    check(pv->x==3,                   "typedef:ptr_struct");
}

void test_inline_static(void) {
    check(inline_sq(5)==25,           "inline:sq");
    check(inline_sq(0)==0,            "inline:zero");
    check((int)inline_dsq(3.0)==9,    "inline:dsq");
    check(static_add(3,4)==7,         "static:add");
    check(static_add(-1,1)==0,        "static:zero");
}

void test_nested_struct(void) {
    struct Outer o;
    o.a.val=10; o.a.pad=0;
    o.b.val=20; o.b.pad=0;
    o.sum=o.a.val+o.b.val;
    check(o.a.val==10,  "nested:a.val");
    check(o.b.val==20,  "nested:b.val");
    check(o.sum==30,    "nested:sum");
}

void test_linked_list(void) {
    struct Node n3; n3.data=3; n3.next=NULL;
    struct Node n2; n2.data=2; n2.next=&n3;
    struct Node n1; n1.data=1; n1.next=&n2;
    int sum=0; int len=0;
    struct Node *cur=&n1;
    while(cur){ sum=sum+cur->data; len++; cur=cur->next; }
    check(sum==6,               "list:sum");
    check(len==3,               "list:len");
    check(n2.next->data==3,     "list:next->data");
}

void test_void_ptr(void) {
    int x=42;
    void *p=(void*)&x;
    int v=*(int*)p;
    check(v==42,                "vptr:int");
    int arr[3]; arr[0]=10; arr[1]=20; arr[2]=30;
    void *ap=(void*)arr;
    int  *ip=(int*)ap;
    check(ip[0]==10&&ip[2]==30, "vptr:array");
    void *np=NULL;
    check(np==NULL,             "vptr:NULL_eq");
    check(!np,                  "vptr:!NULL");
}

void test_auto(void) {
    auto int x=77;
    auto int y=x*2;
    check(x==77,   "auto:int");
    check(y==154,  "auto:computed");
}

void test_bitwise(void) {
    int a=0xAA; int b=0x55;
    check((a&b)==0,       "bit:AND");
    check((a|b)==0xFF,    "bit:OR");
    check((a^b)==0xFF,    "bit:XOR");
    check((~a&0xFF)==0x55,"bit:NOT");
    check((a<<1)==0x154,  "bit:SHL");
    check((a>>1)==0x55,   "bit:SHR");
    unsigned int u=0x80000000U;
    check((u>>31)==1,     "bit:uint_SHR");
    int c=0xF0; c&=0x0F;
    check(c==0,           "bit:AND_assign");
    c|=0xAA;
    check(c==0xAA,        "bit:OR_assign");
    c^=0xFF;
    check(c==0x55,        "bit:XOR_assign");
}

void test_forward_decl(void) {
    check(fwd_double(5)==10,    "fwd:pos");
    check(fwd_double(0)==0,     "fwd:zero");
    check(fwd_double(-3)==-6,   "fwd:neg");
}

void test_union(void) {
    struct Variant v;
    v.type=0; v.as.ival=42;
    check(v.as.ival==42,    "union:ival");
    v.type=1;
    check(v.type==1,        "union:type_field");
}

void test_main_args(int argc, char *argv[]) {
    check(argc>=1,              "argv:argc>=1");
    check(argv[0]!=NULL,        "argv:argv[0]");
    /* Print all argv strings */
    printf("argc=%d\r\n", argc);
    int i;
    for (i = 0; i < argc; i++) {
        printf("argv[%d]=%s\r\n", i, argv[i]);
    }
}

/* forward declaration implementation */
int fwd_double(int x) { return x * 2; }

/* ============================================================ */
int main(int argc, char *argv[]) {
    printf("=== Feature Test Suite ===\r\n");
    test_bool();
    test_int_types();
    test_char_types();
    test_float();
    test_string_functions();
    test_ctype();
    test_stdlib();
    test_typedef();
    test_inline_static();
    test_nested_struct();
    test_linked_list();
    test_void_ptr();
    test_auto();
    test_bitwise();
    test_forward_decl();
    test_union();
    test_main_args(argc, argv);
    printf("===========================\r\n");
    printf("PASS:%d FAIL:%d\r\n", g_pass, g_fail);
    if (g_fail == 0) printf("ALL PASS\r\n");
    else             printf("FAILURES\r\n");
    ExitProcess(0);
    return 0;
}
