#ifndef CPP_RT_H
#define CPP_RT_H

/* =========================================================================
 * cpp_rt — the small hand-written runtime backing squash's C++ frontend
 * (see CPP/cpp_lower.c, and cpp_lower.h's own header comment for the
 * exact, honest scope of what std::string/std::vector<T>/std::cout this
 * frontend understands — this is NOT a real STL).
 *
 * Deliberately plain C, using only what squash's own include/ headers
 * declare (no size_t-heavy signatures beyond what stdio.h/stdlib.h
 * already use as "unsigned int" — see CSR/csharp_rt.h's own comment for
 * why this codebase's convention is 32-bit sizes throughout).
 *
 * NEVER returns a struct BY VALUE anywhere in this file — squash's native
 * C backend has a real, confirmed codegen bug that silently drops fields
 * past the first when a function returns a struct larger than 4 bytes by
 * value (see the fix/workaround note at its own site in codegen.c). Every
 * function here that conceptually "returns" a CppString takes an explicit
 * leading out-pointer instead, exactly like cpp_lower.c's own generated
 * code does for every user-defined class — see cpp_lower.h's header
 * comment for the full rationale. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int cpp_bool;

/* ---- std::string --------------------------------------------------- */
typedef struct { char *data; int len; int cap; } CppString;

void  cpp_string_ctor(CppString *out);
void  cpp_string_ctor_cstr(CppString *out, const char *s);
void  cpp_string_ctor_copy(CppString *out, const CppString *src);
void  cpp_string_dtor(CppString *s);
void  cpp_string_assign(CppString *dst, const CppString *src);
void  cpp_string_assign_cstr(CppString *dst, const char *s);
void  cpp_string_concat(CppString *out, const CppString *a, const CppString *b);
void  cpp_string_concat_cstr(CppString *out, const CppString *a, const char *b);
void  cpp_string_append(CppString *dst, const CppString *b);
void  cpp_string_append_cstr(CppString *dst, const char *b);
int   cpp_string_eq(const CppString *a, const CppString *b);
int   cpp_string_eq_cstr(const CppString *a, const char *b);
int   cpp_string_lt(const CppString *a, const CppString *b);
const char *cpp_string_c_str(const CppString *s);
int   cpp_string_length(const CppString *s);
char  cpp_string_at(const CppString *s, int idx);

/* ---- std::cout / std::cin / std::endl -------------------------------
 * "std::cout << a << b;" lowers to a sequence of these calls, one per
 * "<<" operand, chosen by cpp_lower.c's own static type inference for
 * that operand — see cpp_lower.h's own comment on why chained "<<" can't
 * lower to real operator overloading here (the *output* is plain C). */
void cpp_cout_int(long long v);
void cpp_cout_double(double v);
void cpp_cout_char(int c);
void cpp_cout_cstr(const char *s);
void cpp_cout_string(const CppString *s);
void cpp_cout_bool(int v);
void cpp_cout_endl(void);
void cpp_cin_int(long long *out);
void cpp_cin_double(double *out);
void cpp_cin_string(CppString *out);

/* ---- std::vector<T> --------------------------------------------------
 * Generic-by-element-size backing store; cpp_lower.c emits a distinct,
 * tiny typed wrapper (push_back/size/operator[]/at) per monomorphized
 * "vector<T>" instantiation on top of this ONE untyped implementation —
 * see cpp_lower.h's own comment on template monomorphization. */
typedef struct { void *data; int size; int cap; int elem_size; } CppVecRaw;

void  cpp_vecraw_ctor(CppVecRaw *v, int elem_size);
void  cpp_vecraw_dtor(CppVecRaw *v);
void  cpp_vecraw_push(CppVecRaw *v, const void *elem);
void *cpp_vecraw_at(CppVecRaw *v, int idx);
int   cpp_vecraw_size(CppVecRaw *v);
void  cpp_vecraw_pop_back(CppVecRaw *v);

#endif /* CPP_RT_H */
