#ifndef CPP_PARSER_H
#define CPP_PARSER_H

#include "cpp_lexer.h"
#include "cpp_ast.h"

/* Recursive-descent C++ parser — modeled on CS/cs_parser.h stylistically.
 *
 * GRAMMAR SCOPE (the honest, documented subset — see cpp_lower.h for what
 * happens to each of these once parsed):
 *   - #include (both "<...>" system headers, re-emitted verbatim except a
 *     handful of "logical" standard headers cpp_lower.c understands:
 *     <iostream>, <string>, <vector>, <cstdio>, <cstdlib>, <cmath>,
 *     <cstring>) and #define (re-emitted verbatim, unparsed)
 *   - using namespace X; / using X::Y;
 *   - namespace X { ... } (including nested)
 *   - class/struct with: fields (incl. static), methods (incl. static,
 *     virtual, const-qualified, operator overloads), constructors (incl.
 *     a ": Base(args)" initializer list — ONLY a base-class call, member
 *     initializers in the list are not supported, see cpp_lower.h),
 *     destructors (incl. virtual), single public/private/protected
 *     inheritance from one base class
 *   - function/method overloading (resolved by cpp_lower.c via arg-count
 *     then simple static-type matching — see its own header comment for
 *     the exact algorithm and its limits)
 *   - function templates and class templates (single type parameter list,
 *     monomorphized per distinct instantiation by cpp_lower.c)
 *   - references (T&) as parameters and locals; pointers; const
 *   - new/delete, new[]/delete[]
 *   - ordinary C control flow + expressions, ternary, casts, sizeof
 *
 * NOT supported (a clear parse or lowering error, never silent wrong
 * codegen): multiple inheritance, exceptions (try/catch/throw), RTTI
 * (dynamic_cast/typeid), lambdas, multiple template type parameters,
 * variadic templates, operator overloading beyond the common binary/
 * stream/comparison/index operators, full STL (only std::string,
 * std::vector<T>, std::cout/cin/endl are recognized — see cpp_lower.h). */

typedef struct {
    CppLexer lx;
    CppTok  *cur;
    CppTok  *ahead;      /* one token of lookahead, or NULL */
    int      error_count;
    /* >0 while inside a speculative, backtracking parse attempt (e.g.
     * try_parse_var_decl trying "Type name(...)" against what turns out to
     * be an ordinary function call, or the "(Type)expr" cast-vs-grouped-
     * expr disambiguation in parse_primary) — perror_at() still runs (so
     * the snapshot/restore token-position logic is completely unaffected)
     * but skips both the stderr print and the error_count bump, since the
     * ParserSnapshot restore on failure already discards everything else
     * about the attempt; without this, a source line whose real parse
     * succeeds via the fallback path still printed a confusing, entirely
     * bogus "expected expression" diagnostic for the road not taken. */
    int      suppress_diag;
} CppParser;

void     cpp_parser_init(CppParser *p, const char *src);
void     cpp_parser_free(CppParser *p);
CppNode *cpp_parse_unit(CppParser *p);

#endif /* CPP_PARSER_H */
