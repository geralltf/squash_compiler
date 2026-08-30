#ifndef CS_PARSER_H
#define CS_PARSER_H
#include "cs_ast.h"
#include "cs_lexer.h"

/* Recursive-descent C# parser. Grammar scope (matches the plan's Phase 2
 * scope exactly — see /home/squash/.claude/plans/nested-finding-walrus.md
 * and cs_ast.h's own header comment):
 *
 *   - using directives, a single (optional) namespace block, classes/
 *     structs/interfaces with a base-class + interface list.
 *   - generic type parameters on classes/methods with `where T : ...`
 *     constraints (class/struct/new()/base-type-name).
 *   - fields, auto-properties ({ get; } / { get; set; }), methods,
 *     constructors, static members.
 *   - full expression grammar including lambdas, LINQ METHOD-syntax
 *     chains (no special grammar needed — plain member-call chaining),
 *     string interpolation, generic method/constructor calls (`Foo<int>()`,
 *     `new List<int>()`).
 *   - standard statements: if/else, for, foreach, while, do-while,
 *     switch/case/default, break/continue/return, try/catch/finally/
 *     throw, local var decls (`var` and explicit type).
 *
 * NOT in this grammar (parse-time rejection, not silent misparse): LINQ
 * query syntax (`from x in y select z`), async/await, unsafe, records,
 * pattern matching beyond a plain switch, real attribute semantics
 * (attributes are parsed and discarded, not type-checked), operator
 * overloading, `event`, `yield return`.
 *
 * Generics-vs-comparison disambiguation (the classic C-style-language
 * parsing problem: `Foo<T>` vs `a < b`) is resolved with a bounded
 * lookahead/backtrack: see cs_parser.c's try_parse_generic_args_type()
 * for the exact heuristic and its documented failure modes. */

typedef struct {
    CsLexer  lx;
    CsTok   *cur;
    CsTok   *peeked;    /* one token of lookahead beyond `cur`, or NULL */
    int      error_count;
} CsParser;

void cs_parser_init(CsParser *p, const char *src);
void cs_parser_free(CsParser *p);

/* Parses a whole source file into a CS_UNIT node. On a hard parse error,
 * prints a diagnostic (line + expected-vs-got) to stderr, increments
 * p->error_count, and attempts a best-effort statement/member-level
 * recovery (skip to the next '}' or ';' at the current nesting depth) so
 * a single typo doesn't abort the whole parse — matching squash's own
 * parser_new4.c convention of "keep going, report everything, let the
 * caller decide whether error_count>0 means abort". Returns NULL only if
 * recovery itself was impossible (e.g. truncated input mid-token). */
CsNode *cs_parse_unit(CsParser *p);

#endif /* CS_PARSER_H */
