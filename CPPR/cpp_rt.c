#include "cpp_rt.h"

/* Not declared in squash's own include/stdio.h (only sscanf is) — real
 * external call, confirmed working at runtime the same way printf/scanf
 * calls resolve for any other squash-compiled program. */
int scanf(const char *fmt, ...);

/* ---- std::string --------------------------------------------------- */

static void sgrow(CppString *s, int need) {
    if (s->cap >= need) return;
    int newcap = s->cap ? s->cap * 2 : 16;
    while (newcap < need) newcap *= 2;
    char *nd = malloc(newcap);
    if (s->data) { memcpy(nd, s->data, s->len + 1); free(s->data); }
    s->data = nd;
    s->cap = newcap;
}

void cpp_string_ctor(CppString *out) {
    out->data = malloc(1);
    out->data[0] = '\0';
    out->len = 0;
    out->cap = 1;
}
void cpp_string_ctor_cstr(CppString *out, const char *s) {
    int n = (int)strlen(s);
    out->data = malloc(n + 1);
    memcpy(out->data, s, n + 1);
    out->len = n;
    out->cap = n + 1;
}
void cpp_string_ctor_copy(CppString *out, const CppString *src) {
    cpp_string_ctor_cstr(out, src->data ? src->data : "");
}
void cpp_string_dtor(CppString *s) {
    if (s->data) free(s->data);
    s->data = NULL; s->len = 0; s->cap = 0;
}
void cpp_string_assign(CppString *dst, const CppString *src) {
    sgrow(dst, src->len + 1);
    memcpy(dst->data, src->data, src->len + 1);
    dst->len = src->len;
}
void cpp_string_assign_cstr(CppString *dst, const char *s) {
    int n = (int)strlen(s);
    sgrow(dst, n + 1);
    memcpy(dst->data, s, n + 1);
    dst->len = n;
}
void cpp_string_concat(CppString *out, const CppString *a, const CppString *b) {
    int n = a->len + b->len;
    out->data = malloc(n + 1);
    memcpy(out->data, a->data, a->len);
    memcpy(out->data + a->len, b->data, b->len + 1);
    out->len = n;
    out->cap = n + 1;
}
void cpp_string_concat_cstr(CppString *out, const CppString *a, const char *b) {
    int bl = (int)strlen(b);
    int n = a->len + bl;
    out->data = malloc(n + 1);
    memcpy(out->data, a->data, a->len);
    memcpy(out->data + a->len, b, bl + 1);
    out->len = n;
    out->cap = n + 1;
}
void cpp_string_append(CppString *dst, const CppString *b) {
    sgrow(dst, dst->len + b->len + 1);
    memcpy(dst->data + dst->len, b->data, b->len + 1);
    dst->len += b->len;
}
void cpp_string_append_cstr(CppString *dst, const char *b) {
    int bl = (int)strlen(b);
    sgrow(dst, dst->len + bl + 1);
    memcpy(dst->data + dst->len, b, bl + 1);
    dst->len += bl;
}
int cpp_string_eq(const CppString *a, const CppString *b) { return strcmp(a->data, b->data) == 0; }
int cpp_string_eq_cstr(const CppString *a, const char *b) { return strcmp(a->data, b) == 0; }
int cpp_string_lt(const CppString *a, const CppString *b) { return strcmp(a->data, b->data) < 0; }
const char *cpp_string_c_str(const CppString *s) { return s->data ? s->data : ""; }
int cpp_string_length(const CppString *s) { return s->len; }
char cpp_string_at(const CppString *s, int idx) { return s->data[idx]; }

/* ---- std::cout / std::cin -------------------------------------------- */
void cpp_cout_int(long long v) { printf("%lld", v); }
void cpp_cout_double(double v) { printf("%g", v); }
void cpp_cout_char(int c) { putchar(c); }
/* printf("%s", s), NOT fputs(s, stdout) -- confirmed, real squash codegen
 * bug: stdio.h's fputs macro expands to a static helper that calls the
 * real "extern long write(...)" libc function, and a bodyless function
 * declared with the "extern" keyword (SYM_IMPORT) gets its call site
 * speculatively deferred (RELOC_STATIC_REL32, "maybe a sibling .sqo
 * defines this") whenever it's compiled as part of a "-c" precompiled
 * .sqo -- correct for CS/'s sqo_loader use case (an in-process C# script
 * host resolves it dynamically at load time) but WRONG for this runtime's
 * own use case (cpp_rt.sqo gets statically linked into a real ELF/PE
 * executable via "squash main.cpp CPPR/cpp_rt.*.sqo -o out", where that
 * deferred relocation is never actually resolved against real libc and
 * the call silently becomes a no-op — reproduced directly: a hand-written
 * two-file repro where a .sqo-merged function calling extern-declared
 * write() produced zero output while an identical call to plain
 * (non-"extern"-declared) printf()/malloc() from the same merged .sqo
 * worked correctly). See codegen.c's SYM_IMPORT branch (the "extern"
 * dll-sentinel case, near its own "cg->sqo_precompile" check) for the
 * exact code path — a real fix needs the compiler to distinguish "this
 * .sqo will be loaded by sqo_loader" from "this .sqo will be statically
 * linked", which no flag currently expresses, so this runtime routes
 * around it instead of risking a change to that shared, working path. */
void cpp_cout_cstr(const char *s) { printf("%s", s); }
void cpp_cout_string(const CppString *s) { printf("%s", s->data ? s->data : ""); }
void cpp_cout_bool(int v) { fputs(v ? "true" : "false", stdout); }
void cpp_cout_endl(void) { putchar('\n'); }
void cpp_cin_int(long long *out) { long long v = 0; if (scanf("%lld", &v) < 0) {} *out = v; }
void cpp_cin_double(double *out) { double v = 0; if (scanf("%lf", &v) < 0) {} *out = v; }
void cpp_cin_string(CppString *out) {
    char buf[4096];
    if (scanf("%4095s", buf) < 0) buf[0] = '\0';
    cpp_string_assign_cstr(out, buf);
}

/* ---- std::vector<T> raw backing store ---------------------------------- */
void cpp_vecraw_ctor(CppVecRaw *v, int elem_size) {
    v->data = NULL; v->size = 0; v->cap = 0; v->elem_size = elem_size;
}
void cpp_vecraw_dtor(CppVecRaw *v) {
    if (v->data) free(v->data);
    v->data = NULL; v->size = 0; v->cap = 0;
}
void cpp_vecraw_push(CppVecRaw *v, const void *elem) {
    if (v->size >= v->cap) {
        int newcap = v->cap ? v->cap * 2 : 4;
        v->data = realloc(v->data, newcap * v->elem_size);
        v->cap = newcap;
    }
    char *dst = ((char *)v->data) + v->size * v->elem_size;
    memcpy(dst, elem, v->elem_size);
    v->size++;
}
void *cpp_vecraw_at(CppVecRaw *v, int idx) {
    return ((char *)v->data) + idx * v->elem_size;
}
int cpp_vecraw_size(CppVecRaw *v) { return v->size; }
void cpp_vecraw_pop_back(CppVecRaw *v) { if (v->size > 0) v->size--; }
