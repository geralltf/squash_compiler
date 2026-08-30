#ifndef CS_LOWER_H
#define CS_LOWER_H
#include "cs_ast.h"

/* =========================================================================
 * cs_lower — Phase 3 of the plan (/home/squash/.claude/plans/
 * nested-finding-walrus.md): turns a parsed C# CS_UNIT into a plain C
 * SOURCE-TEXT string that squash's OWN, already-correct lexer/parser/
 * codegen pipeline can compile completely unmodified (compiler.c never
 * changes; codegen.c/codegen_arm64.c never change).
 *
 * This is a deliberate refinement of the plan's "lower C# AST into
 * squash's C AST" wording: generating C TEXT and handing it to squash's
 * real front end achieves exactly that (squash's parser turns this text
 * into the identical ASTNode/TypeInfo/SymTable state a hand-written .c
 * file would produce) while avoiding the far riskier alternative of
 * hand-constructing ASTNode trees directly and trying to replicate every
 * invariant parser_new4.c's symbol-table population currently
 * guarantees for codegen.c to work correctly — several real, previously-
 * unknown squash codegen bugs were found just getting the C# frontend's
 * OWN plain C code compiling correctly this session (see CSR/csharp_rt.h
 * and CS/cs_parser.c's own comments), so minimizing exposure to
 * hand-built, parser-bypassing ASTNode state is a deliberate risk
 * reduction, not a shortcut.
 *
 * Scope of THIS pass (grown incrementally — see the plan's own phased
 * verification list): non-generic classes/structs, fields, auto-
 * properties (backed by a plain field), constructors, static and
 * instance methods, `new`, Console.WriteLine/Write, string literals,
 * basic control flow (if/for/foreach-over-List/while/do-while/switch/
 * break/continue/return), the full expression grammar over int/double/
 * bool/string/object-reference values. Method-call resolution uses a
 * LOCAL, per-method static-type tracker (not full type inference): a
 * variable's class type is known when it's declared with an explicit
 * type, or with `var` initialized directly from `new SomeClass(...)` --
 * anything else (e.g. a method call whose return type would need real
 * inference to resolve a chained call) is reported as a clear lowering
 * error rather than silently emitting wrong code.
 *
 * Explicitly NOT YET in this pass (documented, tracked as follow-up, not
 * silently dropped): generics/monomorphization, LINQ, try/catch/finally,
 * closures/lambdas-as-values, string interpolation beyond simple
 * identifier/literal slots, arrays, interfaces/inheritance dispatch
 * (a `: Base` class lowers its own members fine, but virtual dispatch
 * through a base-typed reference is not implemented). */

typedef struct {
    char  *text;   /* NUL-terminated generated C source, owned by caller */
    int    ok;     /* 1 = lowering completed with no errors */
    int    error_count;
} CsLowerResult;

/* Lowers `unit` to C source text. `runtime_header` is the #include path
 * used for the CSR runtime header (e.g. "csharp_rt.h" or a relative
 * path) -- the generated file's very first line is
 * `#include "<runtime_header>"`. Diagnostics go to stderr, matching
 * cs_parser.c's own perror_at() convention. Caller owns the returned
 * `text` (free() it) and must check `ok` before trying to compile it. */
CsLowerResult cs_lower_unit(CsNode *unit, const char *runtime_header);

#endif /* CS_LOWER_H */
