/*
 * test_diagnostics_fuzzy_match.c
 *
 * Exercises diag.c's diag_suggest_name() (Levenshtein "did you mean 'x'?"
 * spelling suggestion, see [[project_diagnostics_overhaul]]) across the
 * different shapes of typo it needs to actually catch in practice, plus
 * the boundaries of where it should correctly say NOTHING rather than
 * guess. All of these are non-fatal codegen-level errors (squash
 * recovers and keeps compiling), so — like test_diagnostics_errors.c —
 * every case lives in one file and one build shows them all at once.
 *
 * diag_suggest_name() only walks the symbol table's LIVE scope chain
 * (the current scope up through every enclosing one, ending at the
 * global scope) and accepts a candidate within edit distance
 * clamp(badlen/2, 1, 4) of the bad name. Each case below is picked so its
 * expected outcome (suggested / not suggested) follows directly from
 * that rule — see the comment on each function for the arithmetic.
 *
 *  1. Single-character SUBSTITUTION typo   ("lenqth" vs "length")
 *  2. Adjacent-letter TRANSPOSITION typo   ("wdith"  vs "width")
 *  3. Missing-letter (DELETION) typo       ("heigt"  vs "height")
 *  4. Extra-letter (INSERTION) typo        ("ccount" vs "count")
 *  5. Case-only typo                       ("Total"  vs "total")
 *  6. A real PARAMETER name typo (not just a local variable)
 *  7. A real GLOBAL variable typo, referenced from inside a function
 *     (globals live in the outermost scope, an ancestor of every
 *     function's own scope, so they're valid candidates everywhere)
 *  8. NEGATIVE case: nothing in scope is close enough — no suggestion
 *     should be offered at all, rather than an unhelpful wild guess
 *  9. NEGATIVE case: a similarly-spelled variable that exists ONLY in a
 *     different, unrelated function is correctly NOT suggested — the
 *     symbol table scope chain doesn't cross into sibling functions
 */
#include <stdio.h>

static int g_counter = 100; /* real global — see case 7 */

/* Case 1: substitution — length(6) vs lenqth(6), distance 1.
 * threshold = clamp(6/2,1,4) = 3  ->  1 <= 3, suggested. */
static void case1_substitution(void) {
    int length = 10;
    printf("case1: %d\n", lenqth); /* undefined; real typo of 'length' */
    (void)length;
}

/* Case 2: transposition — width(5) vs wdith(5), distance 2 (two
 * substitutions under plain Levenshtein).
 * threshold = clamp(5/2,1,4) = 2  ->  2 <= 2, suggested (edge case). */
static void case2_transposition(void) {
    int width = 20;
    printf("case2: %d\n", wdith); /* undefined; real typo of 'width' */
    (void)width;
}

/* Case 3: deletion — height(6) vs heigt(5), distance 1.
 * threshold = clamp(5/2,1,4) = 2  ->  1 <= 2, suggested. */
static void case3_deletion(void) {
    int height = 30;
    printf("case3: %d\n", heigt); /* undefined; real typo of 'height', missing 'h' */
    (void)height;
}

/* Case 4: insertion — count(5) vs ccount(6), distance 1.
 * threshold = clamp(6/2,1,4) = 3  ->  1 <= 3, suggested. */
static void case4_insertion(void) {
    int count = 40;
    printf("case4: %d\n", ccount); /* undefined; real typo of 'count', extra 'c' */
    (void)count;
}

/* Case 5: case-only typo — total(5) vs Total(5), distance 1 (one
 * substitution: 't' vs 'T'). threshold = clamp(5/2,1,4) = 2  ->  suggested.
 * C is case-sensitive, so 'Total' really is a distinct, undefined
 * identifier here — not just cosmetically different. */
static void case5_case_typo(void) {
    int total = 50;
    printf("case5: %d\n", Total); /* undefined; case-typo of 'total' */
    (void)total;
}

/* Case 6: typo of a real PARAMETER name (not a local variable) —
 * threshold(10) = clamp(10/2,1,4) = 4 -> "threshhold" vs "threshold",
 * distance 1 (one extra 'h'), suggested. Demonstrates candidates include
 * parameters, not just locally-declared variables. */
static int case6_parameter_typo(int threshold) {
    printf("case6: %d\n", threshhold); /* undefined; real typo of param 'threshold' */
    return threshold;
}

/* Case 7: typo of a real GLOBAL variable, referenced from inside an
 * unrelated function — g_counter(9) vs g_countr(8), distance 1 (missing
 * 'e'). threshold = clamp(8/2,1,4) = 4 -> suggested. Globals live in the
 * outermost scope, an ancestor of every function's own scope chain. */
static void case7_global_typo(void) {
    printf("case7: %d\n", g_countr); /* undefined; real typo of global 'g_counter' */
}

/* Case 8 (negative): nothing in scope is remotely close to this name —
 * no suggestion should be offered at all. "value" is the only real
 * candidate in scope; its distance from this bad name is far outside
 * the threshold for either identifier's length. */
static void case8_no_match(void) {
    int value = 60;
    printf("case8: %d\n", zzzqqqxxx_totally_unrelated); /* undefined; nothing close in scope */
    (void)value;
}

/* Case 9 (negative): "shared_value" is real, but only inside
 * case9_producer() — case9_consumer() has its own, separate scope that
 * never encloses case9_producer()'s locals, so a typo of it there must
 * NOT be suggested even though the spelling is close (distance 1). */
static int case9_producer(void) {
    int shared_value = 70;
    return shared_value;
}
static void case9_consumer(void) {
    printf("case9: %d\n", shared_valu); /* undefined; close to 'shared_value', but that's a different function's local */
}

static void case10_two_letter_fuzzy_match(void) {
	int shared_value = 70;
    printf("case10: %d\n", shared_val); 
}
static void case10_three_letter_fuzzy_match(void) {
	int shared_value = 70;
    printf("case10: %d\n", share_val); 
}
int main(void) {
    case1_substitution();
    case2_transposition();
    case3_deletion();
    case4_insertion();
    case5_case_typo();
    case6_parameter_typo(80);
    case7_global_typo();
    case8_no_match();
    case9_consumer();
	case10_two_letter_fuzzy_match();
	case10_three_letter_fuzzy_match();
    return 0;
}
