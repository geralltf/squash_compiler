#ifndef CSHARP_RT_H
#define CSHARP_RT_H

/* =========================================================================
 * csharp_rt — the C# runtime library backing squash's AOT-compiled C#
 * frontend (see CS/cs_lower.c, once it exists — this file is Phase 1 of
 * the plan in /home/squash/.claude/plans/nested-finding-walrus.md, built
 * and tested standalone before any C# source has ever been compiled).
 *
 * Pure C, zero dependency on squash's own compiler internals or on SQW/
 * SQS. Deliberately squash-compilable (no bit-fields, no VLAs, no size_t —
 * this codebase's own headers (include/stdlib.h) declare malloc/realloc
 * etc. taking "unsigned int", not size_t, and every other C runtime piece
 * in this repo follows that same 32-bit-size convention, so this file
 * does too; a single C# script allocating >4GB in one call is out of
 * scope).
 *
 * Four pieces, in the order the lowering pass will need them:
 *   1. GC: mark-sweep, non-moving, with an explicit SHADOW ROOT STACK
 *      (not stack-scanning — squash's codegen has no stack-map/precise-
 *      root support, and the plan deliberately avoided adding any so
 *      both existing codegen backends stay untouched). The lowering pass
 *      is responsible for calling csr_gc_push_root()/csr_gc_pop_root()
 *      around every reference-typed local's scope; the mark phase does
 *      nothing but walk that stack.
 *   2. Exceptions: setjmp/longjmp-based handler chain. Verified (this
 *      session) that squash-compiled code calling the REAL libc setjmp/
 *      longjmp works correctly, by declaring them with a small hand-
 *      written prototype (void* env instead of the real jmp_buf array
 *      type) rather than needing a full <setjmp.h> shim in squash's own
 *      include/ — see CS_JMPBUF_BYTES below for why the buffer is sized
 *      the way it is.
 *   3. Delegates/closures: a plain 2-pointer value struct (function
 *      pointer + capture-struct pointer) — not itself GC-tracked (it's a
 *      small value type copied by value like any other struct); the
 *      CAPTURE struct it points to IS GC-allocated, since a closure can
 *      outlive the scope that created it.
 *   4. A minimal BCL subset: CsString (immutable, GC-allocated, UTF-8),
 *      CsList (generic-erased by element size — the lowering pass emits
 *      distinctly-named typed accessor wrappers per monomorphized
 *      instantiation, e.g. List<int> vs List<Person>, on top of this
 *      same untyped backing store), CsDict (string-keyed, generic-erased
 *      by value size — arbitrary-object keys are a documented follow-up,
 *      not silently unsupported), Console.WriteLine, and the eager LINQ
 *      primitives method-syntax calls desugar into (Where/Select/Count/
 *      Sum/First/FirstOrDefault/Any/All/ToList/OrderBy).
 * ========================================================================= */

/* ---- 1. GC ------------------------------------------------------------ */

/* Every GC-managed allocation is prefixed by this header; csr_gc_alloc()
 * returns a pointer to the PAYLOAD (just past the header), matching the
 * convention every csr_*_new() constructor below follows. */
typedef struct CsObjHeader {
    struct CsObjHeader *gc_next;  /* intrusive list of every live allocation */
    unsigned int         size;     /* total bytes, header included */
    unsigned short        type_id;  /* caller-defined; 0 = untyped/internal */
    unsigned char          kind;     /* CS_KIND_* below */
    unsigned char          mark;     /* GC mark bit; swept if 0 after a mark pass */
} CsObjHeader;

#define CS_KIND_RAW    0   /* opaque payload, no further GC structure (e.g. capture structs with no ref fields) */
#define CS_KIND_STRING 1
#define CS_KIND_LIST   2
#define CS_KIND_DICT   3
#define CS_KIND_OBJECT 4   /* a lowered C# class instance */
#define CS_KIND_ARRAY  5

/* Payload -> header, header -> payload. Every csr_gc_alloc() caller/
 * consumer uses these instead of hand-rolling the pointer arithmetic. */
CsObjHeader *csr_hdr_of(void *payload);
void        *csr_payload_of(CsObjHeader *hdr);

/* Allocate `payload_size` bytes of zero-initialized payload, tagged with
 * `type_id`/`kind`. May trigger a collection first if the heap has grown
 * past its current threshold. Never returns NULL (aborts on real OOM,
 * matching this codebase's own malloc-failure convention elsewhere). */
void *csr_gc_alloc(unsigned int payload_size, unsigned short type_id, unsigned char kind);

/* Shadow root stack. `slot` is the ADDRESS of a local variable holding a
 * (possibly GC-managed) pointer — NOT the pointer's value — so the mark
 * phase always sees the variable's CURRENT value, even if it was
 * reassigned after the push. The lowering pass emits one push per
 * reference-typed local at scope entry and one matching pop at every
 * scope exit (including early return/break/continue paths — same
 * multi-exit-edge duplication already planned for `finally` blocks). */
void csr_gc_push_root(void **slot);
void csr_gc_pop_root(void);

/* Force a collection now (mark every pushed root's current value,
 * transitively, via each kind's own "does this contain other GC
 * pointers" walk — CS_KIND_OBJECT instances need a per-type field-offset
 * table the lowering pass registers via csr_gc_register_type_layout();
 * untyped/CS_KIND_RAW payloads are treated as leaves). Exposed mainly for
 * deterministic unit testing; csr_gc_alloc() calls this on its own once
 * the heap crosses its growth threshold. */
void csr_gc_collect(void);

/* Registers, for a given type_id, which byte offsets within a CS_KIND_
 * OBJECT payload hold other GC pointers (so the mark phase can trace
 * through object graphs, not just the shadow stack's own direct roots).
 * `offsets` is copied; call once per class per program, typically from a
 * generated static-init function (see objfile.h's own __sqx_static_init
 * comment for why lowered code already has a place to put "run this
 * once at startup" logic). */
void csr_gc_register_type_layout(unsigned short type_id, const int *ref_field_offsets, int n_offsets);

/* Test/diagnostic hooks — total bytes currently live, total allocations
 * made since program start, total collections run. */
unsigned int csr_gc_live_bytes(void);
unsigned int csr_gc_alloc_count(void);
unsigned int csr_gc_collect_count(void);

/* ---- 2. Exceptions ------------------------------------------------------
 * A real libc jmp_buf (glibc x86-64) is a __jmp_buf_tag: 8 saved longs +
 * a saved-signal-mask flag + a 128-byte sigset_t, ~200 bytes total. This
 * runtime never inspects a jmp_buf's contents (setjmp/longjmp do that),
 * only ever passes its address around as an opaque blob, so it doesn't
 * need the real struct layout — just enough raw bytes that setjmp() (the
 * REAL libc one, declared below with a hand-written void*-taking
 * prototype instead of needing squash's include/ to gain a real
 * <setjmp.h>; verified this session that squash-compiled code calling
 * real libc setjmp/longjmp this way round-trips correctly) can't write
 * past the end of it. Rounded up generously past glibc's real size. */
#define CS_JMPBUF_BYTES 256

extern int  setjmp(void *env);
extern void longjmp(void *env, int val);

/* NOTE: `buf` is deliberately `unsigned char buf[CS_JMPBUF_BYTES]` written
 * out directly, NOT a typedef'd array type (e.g. `typedef unsigned char
 * CsJmpBuf[256]; ... CsJmpBuf buf;`) used as the field's type. A second
 * real squash codegen bug was found this session: a struct field declared
 * through a typedef'd ARRAY type comes back mis-sized (confirmed minimal
 * repro: a `{ CsJmpBuf buf; void *prev; }`-shaped struct's `prev` field
 * ends up overlapping the middle of `buf`'s real 256 bytes, so a write
 * into `buf` — exactly what setjmp() does — corrupts `prev` and beyond,
 * eventually corrupting the stack; the IDENTICAL field written directly
 * as `unsigned char buf[256];` lays out correctly) even though the same
 * typedef used as a LOCAL variable's or PARAMETER's type works fine.
 * Worked around here rather than fixed in codegen.c/parser_new4.c (out of
 * scope for this runtime — flagged to the user as a second discovered,
 * deferred squash bug). */
typedef struct CsExFrame {
    unsigned char        buf[CS_JMPBUF_BYTES];
    struct CsExFrame   *prev;
} CsExFrame;

/* Lowering pattern for `try { A } catch (T e) { B } finally { C }`:
 *   CsExFrame __f;
 *   csr_try_push(&__f);
 *   if (setjmp(__f.buf) == 0) {
 *       A;
 *       csr_try_pop();
 *       C;                          // finally, normal-exit copy
 *   } else {
 *       csr_try_pop();
 *       if (csr_exception_matches(T_typeid)) {
 *           e = csr_current_exception();
 *           B;
 *           C;                      // finally, caught-exception copy
 *       } else {
 *           C;                      // finally, rethrow copy
 *           csr_rethrow();
 *       }
 *   }
 * (Multiple catch clauses chain as further csr_exception_matches() checks;
 * a bare `finally` with no `catch` skips the matches-check branch and
 * always rethrows after running C.) */
void csr_try_push(CsExFrame *frame);
void csr_try_pop(void);

/* Throws `ex` (a GC-allocated CS_KIND_OBJECT, tagged with its C# type_id
 * for catch-clause matching) by longjmp-ing to the nearest enclosing
 * csr_try_push()'d frame. If none is active, prints an "unhandled
 * exception" diagnostic (mirroring real C#/CLR behavior for an
 * unobserved throw reaching the top of the call stack) and aborts. */
void csr_throw(void *ex_object, unsigned short type_id);

/* True if the currently-in-flight exception's type_id equals `type_id`
 * OR is registered (via csr_register_exception_base()) as a subtype of
 * it — real C# catch matching is by-base-type, e.g. "catch (Exception e)"
 * must also catch a thrown ArgumentException. */
int csr_exception_matches(unsigned short type_id);
void *csr_current_exception(void);
unsigned short csr_current_exception_type(void);
void csr_register_exception_base(unsigned short derived_type_id, unsigned short base_type_id);

/* Re-raises the currently-in-flight exception into the NEXT enclosing
 * frame (the `finally`-without-a-matching-catch / explicit `throw;`
 * case). Must only be called while inside a catch/finally block reached
 * via csr_throw()'s longjmp — i.e. csr_current_exception() must be valid. */
void csr_rethrow(void);

/* ---- 3. Delegates ------------------------------------------------------- */

/* A delegate/closure value. `capture` is NULL for a plain named-method
 * reference with nothing captured (e.g. Func<int,int> f = SomeMethod;);
 * otherwise it points at a GC-allocated, per-lambda-site capture struct
 * built by the lowering pass. Calling convention: fn(capture, args...) —
 * every lowered delegate-typed function pointer takes the capture
 * pointer as its own first argument, whether or not it actually uses it,
 * so a single calling convention covers both cases uniformly. */
typedef struct CsDelegate {
    void *fn;
    void *capture;
} CsDelegate;

/* ---- 4a. CsString -------------------------------------------------------
 * Immutable, GC-allocated, UTF-8 (not UTF-16 like real CLR strings — a
 * documented simplification; nothing in the core+generics+LINQ scope this
 * effort targets needs UTF-16-specific semantics like surrogate pairs).
 * Payload layout: { int len; char data[len+1]; } — data is always NUL-
 * terminated too, so a CsString's `data` can be handed directly to any
 * libc string function when convenient, alongside the explicit length for
 * embedded-NUL-safe operations. */
typedef struct CsString {
    int  len;
    char data[1];   /* actually len+1 bytes, NUL-terminated */
} CsString;

CsString *cs_string_new(const char *cstr);           /* copies, computes len via strlen */
CsString *cs_string_new_len(const char *bytes, int len); /* copies exactly len bytes + NUL */
CsString *cs_string_concat(CsString *a, CsString *b);
int       cs_string_eq(CsString *a, CsString *b);
int       cs_string_cmp(CsString *a, CsString *b);    /* strcmp-shaped, for OrderBy */
CsString *cs_string_from_int(int v);
CsString *cs_string_from_double(double v);
CsString *cs_string_substring(CsString *s, int start, int len);

/* ---- 4b. CsList<T> (generic-erased) -------------------------------------
 * Backing store for every monomorphized List<T>/array-shaped generic
 * instantiation: elements are opaque `elem_size`-byte blobs, copied by
 * value in/out via csr_list_get/set — a reference-typed T (e.g.
 * List<Person>) stores CsObject* pointers as its "value", so the list
 * itself doesn't need to know or care whether T is a value or reference
 * type; the lowering pass's per-instantiation typed wrapper functions
 * are what give callers a real, type-safe C# surface on top of this. */
typedef struct CsList {
    int            elem_size;
    int            count;
    int            cap;
    unsigned char *data;
} CsList;

CsList *csr_list_new(int elem_size, int initial_cap);
void    csr_list_add(CsList *list, const void *elem);
void    csr_list_get(CsList *list, int index, void *out);
void    csr_list_set(CsList *list, int index, const void *elem);
void    csr_list_remove_at(CsList *list, int index);
void    csr_list_clear(CsList *list);
int     csr_list_count(CsList *list);
CsList *csr_list_copy(CsList *list);   /* shallow element-blob copy, fresh backing array */

/* ---- 4c. CsDict (string-keyed, generic-erased by value size) ----------
 * MVP scope: string keys only (Dictionary<string,T>), open addressing
 * with linear probing. Arbitrary object/value-type keys need a real
 * GetHashCode()/Equals() dispatch this phase doesn't build yet — documented
 * follow-up, not silently missing. */
typedef struct CsDict {
    int             val_size;
    int             count;
    int             cap;         /* always a power of two */
    CsString      **keys;        /* NULL slot = empty */
    unsigned char  *used;        /* 1 = occupied (kept separate from keys==NULL
                                   * so a tombstone-free simple design still
                                   * distinguishes "never used" from "empty
                                   * after remove" if remove is added later) */
    unsigned char  *values;
} CsDict;

CsDict *csr_dict_new(int val_size);
void    csr_dict_set(CsDict *d, CsString *key, const void *val);
int     csr_dict_try_get(CsDict *d, CsString *key, void *out); /* 1=found */
int     csr_dict_contains_key(CsDict *d, CsString *key);
int     csr_dict_count(CsDict *d);

/* ---- 4d. Console --------------------------------------------------------- */
void csr_console_write_line(CsString *s);
void csr_console_write(CsString *s);

/* ---- 4e. LINQ (eager method-syntax primitives) --------------------------
 * All operate on/return CsList*; the lowering pass desugars
 * `src.Where(x => pred).Select(x => proj)`-shaped chains into direct
 * calls to these, threading each stage's output list into the next —
 * eager evaluation, not real deferred-execution IEnumerable<T> (see the
 * plan's explicit LINQ scope note). `capture` is passed through to the
 * predicate/projection/comparator delegate unchanged (the closure's own
 * capture pointer, or NULL for a captureless lambda/named method). */
CsList *csr_linq_where(CsList *src, int (*pred)(void *capture, const void *elem), void *capture);
CsList *csr_linq_select(CsList *src, int out_elem_size,
                         void (*proj)(void *capture, const void *elem, void *out), void *capture);
CsList *csr_linq_order_by(CsList *src, int (*cmp)(void *capture, const void *a, const void *b),
                           void *capture, int descending);
int     csr_linq_count(CsList *src);
int     csr_linq_sum_int(CsList *src);
double  csr_linq_sum_double(CsList *src);
int     csr_linq_first(CsList *src, void *out);            /* 1=found (list non-empty) */
int     csr_linq_first_where(CsList *src, int (*pred)(void *capture, const void *elem),
                              void *capture, void *out);    /* 1=found */
int     csr_linq_any(CsList *src, int (*pred)(void *capture, const void *elem), void *capture);
int     csr_linq_all(CsList *src, int (*pred)(void *capture, const void *elem), void *capture);
CsList *csr_linq_to_list(CsList *src);   /* alias for csr_list_copy, spelled the LINQ way */

#endif /* CSHARP_RT_H */
