/* Standalone unit tests for CSR/csharp_rt.c — built with BOTH gcc and
 * squash (this project's standing verification convention: see
 * project_sqs_wordpress.md memory notes for prior examples), asserting
 * identical pass/fail output from both. No SQW/SQS/compiler.c dependency
 * at all — pure runtime-library test. */
#include "../csharp_rt.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("[OK]   %s\n", msg); } \
    else      { printf("[FAIL] %s\n", msg); g_fail = 1; } \
} while (0)

/* ---- GC: alloc/collect roundtrip ---- */
static void test_gc_basic(void) {
    CsString *kept = 0;
    csr_gc_push_root((void **)&kept);
    kept = cs_string_new("i-am-rooted");

    /* allocate a pile of unrooted garbage past the initial threshold to
     * force at least one real collection */
    {
        int i;
        for (i = 0; i < 200000; i++) {
            CsString *garbage = cs_string_new("throwaway");
            (void)garbage; /* never rooted -- collectible immediately */
        }
    }
    csr_gc_collect();

    CHECK(kept != 0 && cs_string_eq(kept, cs_string_new("i-am-rooted")), "gc: rooted string survives a collection");
    CHECK(csr_gc_collect_count() >= 1, "gc: at least one collection actually ran");
    csr_gc_pop_root();

    /* after popping the root and collecting again, live bytes should
     * drop back down close to baseline (allow generous slack -- this
     * isn't a leak-detector, just a sanity check that sweeping happens) */
    {
        unsigned int before = csr_gc_live_bytes();
        csr_gc_collect();
        unsigned int after = csr_gc_live_bytes();
        CHECK(after <= before, "gc: unrooted-after-pop string is reclaimed (or at least not growing)");
    }
}

/* ---- Exceptions: try/throw/catch/finally ---- */
#define TYPE_MY_EX 100
#define TYPE_BASE_EX 1

static CsString *g_trace;
static void trace(const char *s) {
    CsString *piece = cs_string_new(s);
    CsString *next = cs_string_concat(g_trace, piece);
    csr_gc_pop_root(); /* pop the old g_trace root before repointing it */
    g_trace = next;
    csr_gc_push_root((void **)&g_trace);
}

static void throwing_inner(void) {
    trace("inner-before-throw;");
    csr_throw((void *)cs_string_new("boom"), TYPE_MY_EX);
    trace("SHOULD-NOT-RUN;");
}

static void test_exceptions(void) {
    CsExFrame frame;
    g_trace = cs_string_new("");
    csr_gc_push_root((void **)&g_trace);

    csr_try_push(&frame);
    if (setjmp(frame.buf) == 0) {
        throwing_inner();
        csr_try_pop();
        trace("finally-normal;"); /* not reached: throwing_inner always throws */
    } else {
        /* No csr_try_pop() here -- csr_throw() already popped this
         * frame before its longjmp landed here (see csharp_rt.h's own
         * corrected template comment above csr_try_push()'s
         * declaration). */
        if (csr_exception_matches(TYPE_MY_EX)) {
            CsString *msg = (CsString *)csr_current_exception();
            trace("caught:");
            {
                CsString *joined = cs_string_concat(g_trace, msg);
                csr_gc_pop_root();
                g_trace = joined;
                csr_gc_push_root((void **)&g_trace);
            }
            trace(";finally;");
        } else {
            trace("wrong-type;");
        }
    }

    {
        CsString *expected = cs_string_new("inner-before-throw;caught:boom;finally;");
        CHECK(cs_string_eq(g_trace, expected), "exceptions: try/throw/catch/finally executes expected sequence");
    }
    csr_gc_pop_root();
}

static void test_exception_base_matching(void) {
    csr_register_exception_base(200 /* ArgumentException-ish */, TYPE_BASE_EX /* Exception-ish */);
    {
        CsExFrame frame;
        int matched_as_base = 0;
        csr_try_push(&frame);
        if (setjmp(frame.buf) == 0) {
            csr_throw((void *)cs_string_new("bad-arg"), 200);
        } else {
            /* No csr_try_pop() here either -- see test_exceptions()'s own
             * comment. */
            matched_as_base = csr_exception_matches(TYPE_BASE_EX);
        }
        CHECK(matched_as_base, "exceptions: catch(BaseException) matches a thrown derived type");
    }
}

/* Regression guard for a real bug found this session (in CS/cs_lower.c's
 * first CS_TRY lowering, copied from what used to be documented in
 * csharp_rt.h's own template comment): an inner try/finally with NO
 * catch of its own must re-raise into the OUTER frame via csr_rethrow(),
 * and the outer frame must still be reachable to catch it -- exactly the
 * shape a redundant csr_try_pop() in the exceptional branch breaks (it
 * double-pops, since csr_throw()/csr_rethrow() already popped the
 * frame they jump into before the longjmp), even though the exact same
 * mistake is invisible in every OTHER test in this file (a single,
 * non-nested try, where the extra pop is a harmless no-op). */
static void test_nested_try_finally_rethrow(void) {
    int outer_finally_ran = 0, inner_finally_ran = 0, caught = 0;
    CsExFrame outer;
    csr_try_push(&outer);
    if (setjmp(outer.buf) == 0) {
        CsExFrame inner;
        csr_try_push(&inner);
        if (setjmp(inner.buf) == 0) {
            csr_throw((void *)cs_string_new("nested-boom"), TYPE_MY_EX);
            csr_try_pop();
        } else {
            inner_finally_ran = 1;
            csr_rethrow();
        }
        csr_try_pop();
    } else {
        if (csr_exception_matches(TYPE_MY_EX)) caught = 1;
    }
    outer_finally_ran = 1;
    CHECK(inner_finally_ran, "nested try: inner finally ran before rethrow");
    CHECK(caught, "nested try: outer catch still reachable after inner rethrow (regression guard)");
    CHECK(outer_finally_ran, "nested try: control returns normally after the outer catch handles it");
}

/* ---- Delegates/closures ---- */
typedef struct { int captured; } AddCapture;
static int add_captured(void *capture, int x) {
    AddCapture *c = (AddCapture *)capture;
    return c->captured + x;
}

static void test_delegates(void) {
    AddCapture *cap = (AddCapture *)csr_gc_alloc(sizeof(AddCapture), 0, CS_KIND_RAW);
    CsDelegate d;
    cap->captured = 10;
    d.fn = (void *)add_captured;
    d.capture = cap;
    {
        typedef int (*fnty)(void *, int);
        int result = ((fnty)d.fn)(d.capture, 5);
        CHECK(result == 15, "delegates: capture struct correctly threaded through call");
    }
}

/* ---- CsList ---- */
static void test_list(void) {
    CsList *l = csr_list_new(sizeof(int), 2);
    int i;
    for (i = 0; i < 10; i++) csr_list_add(l, &i);
    CHECK(csr_list_count(l) == 10, "list: count after 10 adds (past initial cap, forces growth)");
    {
        int v; csr_list_get(l, 7, &v);
        CHECK(v == 7, "list: element at index 7 round-trips correctly");
    }
    csr_list_remove_at(l, 0);
    {
        int v; csr_list_get(l, 0, &v);
        CHECK(csr_list_count(l) == 9 && v == 1, "list: remove_at(0) shifts remaining elements");
    }
}

/* ---- CsDict ---- */
static void test_dict(void) {
    CsDict *d = csr_dict_new(sizeof(int));
    int i;
    for (i = 0; i < 50; i++) {
        char buf[32]; snprintf(buf, sizeof buf, "key%d", i);
        csr_dict_set(d, cs_string_new(buf), &i);
    }
    CHECK(csr_dict_count(d) == 50, "dict: count after 50 inserts (forces multiple grows)");
    {
        int v = -1;
        int found = csr_dict_try_get(d, cs_string_new("key37"), &v);
        CHECK(found && v == 37, "dict: try_get finds a key inserted early, after growth");
    }
    CHECK(!csr_dict_contains_key(d, cs_string_new("nope")), "dict: contains_key false for missing key");
    /* overwrite */
    { int nv = 999; csr_dict_set(d, cs_string_new("key37"), &nv); }
    {
        int v = -1;
        csr_dict_try_get(d, cs_string_new("key37"), &v);
        CHECK(v == 999 && csr_dict_count(d) == 50, "dict: set on existing key overwrites, doesn't grow count");
    }
}

/* ---- LINQ ---- */
static int is_even(void *capture, const void *elem) {
    int v; (void)capture; memcpy(&v, elem, sizeof v); return (v % 2) == 0;
}
static void double_it(void *capture, const void *elem, void *out) {
    int v; (void)capture; memcpy(&v, elem, sizeof v); v *= 2; memcpy(out, &v, sizeof v);
}
static int int_cmp(void *capture, const void *a, const void *b) {
    int va, vb; (void)capture; memcpy(&va, a, sizeof va); memcpy(&vb, b, sizeof vb);
    return va - vb;
}

static void test_linq(void) {
    CsList *src = csr_list_new(sizeof(int), 4);
    int vals[] = {5, 3, 8, 1, 4, 9, 2, 7, 6};
    int i;
    for (i = 0; i < 9; i++) csr_list_add(src, &vals[i]);

    {
        CsList *evens = csr_linq_where(src, is_even, 0);
        CHECK(csr_linq_count(evens) == 4, "linq: Where(even) yields 4 of 9 elements");
    }
    {
        CsList *evens = csr_linq_where(src, is_even, 0);
        CsList *doubled = csr_linq_select(evens, sizeof(int), double_it, 0);
        int v0; csr_list_get(doubled, 0, &v0);
        CHECK(v0 % 4 == 0, "linq: Where().Select(double) chain produces multiples of 4");
    }
    {
        CsList *sorted = csr_linq_order_by(src, int_cmp, 0, 0);
        int v0, v8;
        csr_list_get(sorted, 0, &v0);
        csr_list_get(sorted, 8, &v8);
        CHECK(v0 == 1 && v8 == 9, "linq: OrderBy ascending puts min first, max last");
    }
    {
        CsList *sorted_desc = csr_linq_order_by(src, int_cmp, 0, 1);
        int v0; csr_list_get(sorted_desc, 0, &v0);
        CHECK(v0 == 9, "linq: OrderByDescending puts max first");
    }
    CHECK(csr_linq_sum_int(src) == 45, "linq: Sum over 1..9 == 45");
    {
        int first; int found = csr_linq_first(src, &first);
        CHECK(found && first == 5, "linq: First returns the first element as-inserted");
    }
    CHECK(csr_linq_any(src, is_even, 0), "linq: Any(even) true");
    CHECK(!csr_linq_all(src, is_even, 0), "linq: All(even) false (mixed list)");
}

int main(void) {
    test_gc_basic();
    test_exceptions();
    test_exception_base_matching();
    test_nested_try_finally_rethrow();
    test_delegates();
    test_list();
    test_dict();
    test_linq();

    if (g_fail) { printf("=== RESULT: FAIL ===\n"); return 1; }
    printf("=== RESULT: PASS ===\n");
    return 0;
}
