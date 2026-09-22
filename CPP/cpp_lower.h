#ifndef CPP_LOWER_H
#define CPP_LOWER_H

#include "cpp_ast.h"

/* =========================================================================
 * cpp_lower — turns a cpp_ast.h tree into plain C SOURCE TEXT, the same
 * mechanism CS/cs_lower.h uses for C#: the caller (compiler.c, right where
 * it would otherwise read a ".c" file) treats the returned text exactly
 * like any other C translation unit — same preprocess/lex/parse/codegen
 * pipeline, completely unmodified. See that file's own header comment for
 * why text generation (not hand-built ASTNode trees) is the chosen
 * mechanism.
 *
 * LOWERING STRATEGY (the "honest scope" — what a .cpp file can actually
 * contain; anything past this is a clear lowering error, never silent
 * wrong codegen):
 *
 *   - Every class/struct becomes a plain C struct. Single inheritance is
 *     first-member embedding ("struct Derived { struct Base __base; ...
 *     }"), the classic C-with-classes trick that makes a Derived* and the
 *     address of its embedded Base both alias the same bytes.
 *   - Every non-static method/ctor/dtor becomes a free function taking an
 *     explicit "void *__this" first parameter (cast to the real class
 *     pointer type at the top of the body) — NOT a typed "ClassName*"
 *     first parameter — specifically so every virtual method in a whole
 *     hierarchy shares one uniform C function-pointer signature for its
 *     vtable slot, regardless of which class in the hierarchy actually
 *     implements it.
 *   - Virtual dispatch: a hierarchy's vtable TYPE (list of slots) is fixed
 *     by whichever class first declares a virtual method (the "vtable
 *     root") — every class below it may OVERRIDE those slots but may NOT
 *     introduce a brand new virtual method the root didn't declare (a
 *     lowering error: "new virtual method below the vtable root"). This
 *     keeps every vtable in one hierarchy the exact same C struct type,
 *     so no unsafe cross-type vtable-pointer casting is ever needed.
 *   - Function/method/operator overloading is resolved by cpp_lower.c
 *     itself at each call site (arg count, then a coarse type-category
 *     match — int-like vs double vs pointer vs a specific class name; NOT
 *     real overload resolution with implicit conversions/ranking) — an
 *     ambiguous or unmatched call is a lowering error.
 *   - Any method/operator/function whose return type is a class type is
 *     rewritten to return void through a hidden leading "void *__out"
 *     pointer instead — squash's native C backend has a real, separate
 *     codegen bug returning structs >4 bytes by value (confirmed and
 *     documented at its own fix site in codegen.c); this out-pointer
 *     rewrite sidesteps it entirely rather than depending on that fix.
 *     Expressions that use such a call's result as a value (not just a
 *     bare statement) get an automatically-hoisted compiler-generated
 *     temporary — "Point c = a + b;" works via a hidden temp exactly like
 *     "Point c; Point__op_add(&c, &a, &b);" would, and this hoisting
 *     composes for nested expressions ("(a + b) + c2").
 *   - References (T&) are plain pointers under the hood, with the lexical
 *     transparency (implicit address-of at the binding site, implicit
 *     deref at every use) handled entirely by cpp_lower.c's own type
 *     environment — nothing about this is visible in the .cpp source.
 *   - new/delete become malloc+ctor-call / dtor-call+free (new[]/delete[]
 *     likewise, tracking the element count just before the returned
 *     pointer the way a real implementation tracks an allocation's size,
 *     so delete[] knows how many destructors to run).
 *   - Templates (function and class) are monomorphized: cpp_lower.c never
 *     emits a generic definition, only concrete per-instantiation copies
 *     (e.g. "Stack<int>" -> a real "Stack__int" struct + functions),
 *     memoized so repeated uses of the same instantiation share one
 *     definition. Only a single template type parameter is supported.
 *   - #include: a small fixed set of "logical" standard headers this
 *     frontend actually understands (<iostream>, <string>, <vector>,
 *     <cstdio>, <cstdlib>, <cmath>, <cstring>) are swallowed and replaced
 *     by cpp_rt.h + real libc headers as needed; every other #include
 *     (system or user) is re-emitted verbatim, letting real C headers be
 *     included for native interop exactly like a .c file can.
 *   - std::string, std::vector<T>, and std::cout/std::cin/std::endl are
 *     recognized as special forms lowered directly to cpp_rt.h calls (a
 *     small hand-written runtime, see CPPR/cpp_rt.h) — this is NOT a real
 *     STL; any other std:: name is a lowering error.
 *
 * NOT supported at all (documented deferrals, matching CS/cs_lower.h's
 * own convention of failing loudly rather than silently): multiple
 * inheritance, exceptions, RTTI (dynamic_cast/typeid), lambdas, multiple
 * template type parameters, variadic templates, operator overloading
 * beyond the common binary/comparison/stream/index/assignment operators,
 * member initializer lists (only a ": Base(args)" base-constructor call
 * is supported, not ": field(value)" member initializers), local arrays,
 * anything from the STL beyond string/vector/iostream. */

typedef struct {
    int   ok;
    char *text;          /* generated C source text; valid only if ok */
    int   error_count;
} CppLowerResult;

/* `rt_header_name` is the #include line cpp_lower.c emits for the runtime
 * (e.g. "CPPR/cpp_rt.h") — mirrors cs_lower_unit()'s own "csharp_rt.h"
 * parameter for the identical reason: keeps the runtime's location a
 * caller-supplied string instead of a hardcoded path. */
CppLowerResult cpp_lower_unit(CppNode *unit, const char *rt_header_name);

#endif /* CPP_LOWER_H */
