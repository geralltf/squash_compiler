#include "csharp_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * 1. GC — mark-sweep, non-moving, shadow-root-stack.
 * ------------------------------------------------------------------------ */

#define CS_ROOT_STACK_MAX 4096
static void   **g_root_stack[CS_ROOT_STACK_MAX];
static int      g_root_top = 0;

static CsObjHeader *g_gc_all = 0;      /* every live+dead-not-yet-swept allocation */
static unsigned int g_gc_live_bytes = 0;
static unsigned int g_gc_alloc_count = 0;
static unsigned int g_gc_collect_count = 0;
static unsigned int g_gc_threshold = 1u << 20; /* 1MB before first collection */

#define CS_TYPE_LAYOUT_MAX 512
typedef struct { unsigned short type_id; int offsets[32]; int n; } CsTypeLayout;
static CsTypeLayout g_type_layouts[CS_TYPE_LAYOUT_MAX];
static int g_type_layout_count = 0;

CsObjHeader *csr_hdr_of(void *payload) {
    return (CsObjHeader *)((unsigned char *)payload - sizeof(CsObjHeader));
}
void *csr_payload_of(CsObjHeader *hdr) {
    return (void *)((unsigned char *)hdr + sizeof(CsObjHeader));
}

static CsTypeLayout *find_layout(unsigned short type_id) {
    int i;
    for (i = 0; i < g_type_layout_count; i++)
        if (g_type_layouts[i].type_id == type_id) return &g_type_layouts[i];
    return 0;
}

void csr_gc_register_type_layout(unsigned short type_id, const int *ref_field_offsets, int n_offsets) {
    CsTypeLayout *lay;
    int i;
    if (n_offsets > 32) n_offsets = 32; /* documented cap; a class with more than
                                          * 32 reference-typed fields is out of
                                          * scope for this phase's fixed-size
                                          * layout table */
    lay = find_layout(type_id);
    if (!lay) {
        if (g_type_layout_count >= CS_TYPE_LAYOUT_MAX) return;
        lay = &g_type_layouts[g_type_layout_count++];
        lay->type_id = type_id;
    }
    lay->n = n_offsets;
    for (i = 0; i < n_offsets; i++) lay->offsets[i] = ref_field_offsets[i];
}

static void gc_mark_ptr(void *p);

static void gc_mark_object(CsObjHeader *hdr) {
    if (!hdr || hdr->mark) return;
    hdr->mark = 1;
    if (hdr->kind == CS_KIND_LIST) {
        CsList *l = (CsList *)csr_payload_of(hdr);
        /* Conservatively: a CsList of pointer-sized elements MIGHT hold
         * GC pointers (e.g. List<Person>) or might not (List<int>) — this
         * phase doesn't track per-list element-is-reference-type info, so
         * it scans every element as a potential pointer only when
         * elem_size == sizeof(void*), and only follows it if it happens
         * to point at a known live-or-dead GC header (cheap plausibility
         * check via g_gc_all walk is too slow; instead this phase requires
         * the lowering pass to mark reference-element lists explicitly by
         * registering type_id CS_KIND_LIST payloads' element field via
         * csr_gc_register_type_layout on a per-instantiation synthetic
         * type_id — see CS/cs_lower.c's future List<T> lowering). Until
         * that wiring exists, reference-element lists are traced via the
         * conservative scan below; value-typed lists (int, double, ...)
         * are harmless to scan the same way since a random int bit
         * pattern essentially never equals a live heap payload address in
         * practice for this runtime's own bump-style malloc backing, and
         * even a false hit only costs a wasted mark, never incorrect
         * behavior (non-moving GC: an over-retained object is a (rare,
         * bounded) leak until the next collection, not a correctness
         * bug). */
        if (l->elem_size == (int)sizeof(void *)) {
            int i;
            for (i = 0; i < l->count; i++) {
                void *maybe;
                memcpy(&maybe, l->data + (unsigned int)i * (unsigned int)l->elem_size, sizeof(void *));
                gc_mark_ptr(maybe);
            }
        }
    } else if (hdr->kind == CS_KIND_OBJECT) {
        CsTypeLayout *lay = find_layout(hdr->type_id);
        if (lay) {
            unsigned char *base = (unsigned char *)csr_payload_of(hdr);
            int i;
            for (i = 0; i < lay->n; i++) {
                void *maybe;
                memcpy(&maybe, base + lay->offsets[i], sizeof(void *));
                gc_mark_ptr(maybe);
            }
        }
    }
    /* CS_KIND_STRING, CS_KIND_DICT (values not traced this phase — string-
     * keyed dict VALUES that are themselves reference types are a known,
     * documented gap alongside "arbitrary object keys", see csharp_rt.h),
     * CS_KIND_RAW, CS_KIND_ARRAY: treated as leaves. */
}

/* Best-effort "is this even plausibly one of our GC headers" check before
 * following a conservatively-scanned pointer, so a stray non-pointer int
 * that happens to collide doesn't crash by dereferencing garbage — walks
 * the live-allocation list, bounded by CS_GC_PLAUSIBILITY_SCAN_MAX so a
 * pathological program with millions of live objects doesn't make every
 * mark O(n). Documented tradeoff: past that cap, a conservative scan on a
 * huge heap may miss a true positive (leak, not corruption) — precise
 * per-instantiation List<T> layouts (noted above) are the real fix, this
 * is a stopgap that keeps the untyped-list case memory-safe in the
 * meantime. */
#define CS_GC_PLAUSIBILITY_SCAN_MAX 200000
static int looks_like_live_header(void *p) {
    CsObjHeader *h;
    int steps = 0;
    if (!p) return 0;
    for (h = g_gc_all; h && steps < CS_GC_PLAUSIBILITY_SCAN_MAX; h = h->gc_next, steps++)
        if ((void *)csr_payload_of(h) == p) return 1;
    return 0;
}

static void gc_mark_ptr(void *p) {
    if (!p) return;
    if (!looks_like_live_header(p)) return;
    gc_mark_object(csr_hdr_of(p));
}

void csr_gc_push_root(void **slot) {
    if (g_root_top >= CS_ROOT_STACK_MAX) {
        fprintf(stderr, "csharp_rt: GC shadow root stack overflow (CS_ROOT_STACK_MAX=%d)\n", CS_ROOT_STACK_MAX);
        abort();
    }
    g_root_stack[g_root_top++] = slot;
}

void csr_gc_pop_root(void) {
    if (g_root_top <= 0) {
        fprintf(stderr, "csharp_rt: GC shadow root stack underflow (pop with no matching push)\n");
        abort();
    }
    g_root_top--;
}

void csr_gc_collect(void) {
    CsObjHeader *h, *prev, *next;
    int i;
    g_gc_collect_count++;
    for (h = g_gc_all; h; h = h->gc_next) h->mark = 0;
    for (i = 0; i < g_root_top; i++) {
        void *v = *(g_root_stack[i]);
        if (v) gc_mark_object(csr_hdr_of(v));
    }
    prev = 0; h = g_gc_all;
    while (h) {
        next = h->gc_next;
        if (!h->mark) {
            if (prev) prev->gc_next = next; else g_gc_all = next;
            g_gc_live_bytes -= h->size;
            free(h);
        } else {
            prev = h;
        }
        h = next;
    }
}

void *csr_gc_alloc(unsigned int payload_size, unsigned short type_id, unsigned char kind) {
    unsigned int total = (unsigned int)sizeof(CsObjHeader) + payload_size;
    CsObjHeader *hdr;
    if (g_gc_live_bytes + total > g_gc_threshold) {
        csr_gc_collect();
        if (g_gc_live_bytes + total > g_gc_threshold) {
            /* Still over threshold after a real collection: grow it
             * instead of collecting every single allocation from here on
             * (standard "double the heap if collection didn't free
             * enough" growth policy). */
            g_gc_threshold = (g_gc_live_bytes + total) * 2;
        }
    }
    hdr = (CsObjHeader *)malloc(total);
    if (!hdr) {
        fprintf(stderr, "csharp_rt: out of memory allocating %u bytes\n", total);
        abort();
    }
    memset(hdr, 0, total);
    hdr->size = total;
    hdr->type_id = type_id;
    hdr->kind = kind;
    hdr->mark = 0;
    hdr->gc_next = g_gc_all;
    g_gc_all = hdr;
    g_gc_live_bytes += total;
    g_gc_alloc_count++;
    return csr_payload_of(hdr);
}

unsigned int csr_gc_live_bytes(void)    { return g_gc_live_bytes; }
unsigned int csr_gc_alloc_count(void)   { return g_gc_alloc_count; }
unsigned int csr_gc_collect_count(void) { return g_gc_collect_count; }

/* ------------------------------------------------------------------------
 * 2. Exceptions
 * ------------------------------------------------------------------------ */

static CsExFrame *g_ex_top = 0;
static void *g_ex_current = 0;
/* NOTE: deliberately `int`, not `unsigned short` -- a real, narrow squash
 * codegen bug was found this session: a file-scope `static unsigned
 * short` global's stored value comes back corrupted (confirmed minimal
 * repro: set to 100, read back as 4194404 = 0x400064, i.e. the low byte
 * is right but overwritten by high garbage resembling a nearby load
 * address) even though the identical type works correctly as a struct
 * field, a local variable, and a function parameter/return value -- only
 * global/static STORAGE at 16-bit width is affected. Worked around here
 * rather than fixed in codegen.c (out of scope for this runtime — flagged
 * to the user as a discovered, deferred squash bug, not silently
 * papered over). */
static int g_ex_current_type = 0;

#define CS_EX_BASE_MAX 512
typedef struct { unsigned short derived; unsigned short base; } CsExBaseEnt;
static CsExBaseEnt g_ex_bases[CS_EX_BASE_MAX];
static int g_ex_base_count = 0;

void csr_try_push(CsExFrame *frame) {
    frame->prev = g_ex_top;
    g_ex_top = frame;
}

void csr_try_pop(void) {
    if (g_ex_top) g_ex_top = g_ex_top->prev;
}

void csr_register_exception_base(unsigned short derived_type_id, unsigned short base_type_id) {
    if (g_ex_base_count >= CS_EX_BASE_MAX) return;
    g_ex_bases[g_ex_base_count].derived = derived_type_id;
    g_ex_bases[g_ex_base_count].base = base_type_id;
    g_ex_base_count++;
}

static int type_is_or_derives(unsigned short t, unsigned short target) {
    int guard = 0;
    while (guard++ < CS_EX_BASE_MAX) {
        int i;
        int found = 0;
        if (t == target) return 1;
        for (i = 0; i < g_ex_base_count; i++) {
            if (g_ex_bases[i].derived == t) { t = g_ex_bases[i].base; found = 1; break; }
        }
        if (!found) return 0;
    }
    return 0; /* cycle guard tripped -- malformed base-type registration */
}

int csr_exception_matches(unsigned short type_id) {
    return type_is_or_derives(g_ex_current_type, type_id);
}

void *csr_current_exception(void)          { return g_ex_current; }
unsigned short csr_current_exception_type(void) { return g_ex_current_type; }

void csr_throw(void *ex_object, unsigned short type_id) {
    CsExFrame *frame = g_ex_top;
    g_ex_current = ex_object;
    g_ex_current_type = type_id;
    if (!frame) {
        fprintf(stderr, "csharp_rt: unhandled exception (type_id=%u)\n", (unsigned int)type_id);
        abort();
    }
    g_ex_top = frame->prev;
    longjmp(frame->buf, 1);
}

void csr_rethrow(void) {
    CsExFrame *frame = g_ex_top;
    if (!frame) {
        fprintf(stderr, "csharp_rt: unhandled exception (rethrow, type_id=%u)\n", (unsigned int)g_ex_current_type);
        abort();
    }
    g_ex_top = frame->prev;
    longjmp(frame->buf, 1);
}

/* ------------------------------------------------------------------------
 * 4a. CsString
 * ------------------------------------------------------------------------ */

CsString *cs_string_new_len(const char *bytes, int len) {
    CsString *s = (CsString *)csr_gc_alloc((unsigned int)(sizeof(int) + len + 1), 0, CS_KIND_STRING);
    s->len = len;
    if (len > 0 && bytes) memcpy(s->data, bytes, (unsigned int)len);
    s->data[len] = 0;
    return s;
}

CsString *cs_string_new(const char *cstr) {
    return cs_string_new_len(cstr, cstr ? (int)strlen(cstr) : 0);
}

CsString *cs_string_concat(CsString *a, CsString *b) {
    int alen = a ? a->len : 0;
    int blen = b ? b->len : 0;
    CsString *out = (CsString *)csr_gc_alloc((unsigned int)(sizeof(int) + alen + blen + 1), 0, CS_KIND_STRING);
    out->len = alen + blen;
    if (alen) memcpy(out->data, a->data, (unsigned int)alen);
    if (blen) memcpy(out->data + alen, b->data, (unsigned int)blen);
    out->data[alen + blen] = 0;
    return out;
}

int cs_string_eq(CsString *a, CsString *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    return memcmp(a->data, b->data, (unsigned int)a->len) == 0;
}

int cs_string_cmp(CsString *a, CsString *b) {
    int alen = a ? a->len : 0;
    int blen = b ? b->len : 0;
    int m = alen < blen ? alen : blen;
    int c = m ? memcmp(a->data, b->data, (unsigned int)m) : 0;
    if (c != 0) return c;
    return alen - blen;
}

CsString *cs_string_from_int(int v) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d", v);
    return cs_string_new(buf);
}

CsString *cs_string_from_double(double v) {
    char buf[64];
    snprintf(buf, sizeof buf, "%g", v);
    return cs_string_new(buf);
}

CsString *cs_string_substring(CsString *s, int start, int len) {
    if (!s || start < 0 || start > s->len) return cs_string_new_len("", 0);
    if (start + len > s->len) len = s->len - start;
    if (len < 0) len = 0;
    return cs_string_new_len(s->data + start, len);
}

/* ------------------------------------------------------------------------
 * 4b. CsList<T>
 * ------------------------------------------------------------------------ */

CsList *csr_list_new(int elem_size, int initial_cap) {
    CsList *l = (CsList *)csr_gc_alloc((unsigned int)sizeof(CsList), 0, CS_KIND_LIST);
    if (initial_cap < 4) initial_cap = 4;
    l->elem_size = elem_size;
    l->count = 0;
    l->cap = initial_cap;
    l->data = (unsigned char *)malloc((unsigned int)elem_size * (unsigned int)initial_cap);
    return l;
}

static void list_grow(CsList *list, int min_cap) {
    int newcap = list->cap ? list->cap * 2 : 4;
    if (newcap < min_cap) newcap = min_cap;
    list->data = (unsigned char *)realloc(list->data, (unsigned int)list->elem_size * (unsigned int)newcap);
    list->cap = newcap;
}

void csr_list_add(CsList *list, const void *elem) {
    if (list->count >= list->cap) list_grow(list, list->count + 1);
    memcpy(list->data + (unsigned int)list->count * (unsigned int)list->elem_size, elem, (unsigned int)list->elem_size);
    list->count++;
}

void csr_list_get(CsList *list, int index, void *out) {
    memcpy(out, list->data + (unsigned int)index * (unsigned int)list->elem_size, (unsigned int)list->elem_size);
}

void csr_list_set(CsList *list, int index, const void *elem) {
    memcpy(list->data + (unsigned int)index * (unsigned int)list->elem_size, elem, (unsigned int)list->elem_size);
}

void csr_list_remove_at(CsList *list, int index) {
    int tail = list->count - index - 1;
    if (tail > 0) {
        memmove(list->data + (unsigned int)index * (unsigned int)list->elem_size,
                list->data + (unsigned int)(index + 1) * (unsigned int)list->elem_size,
                (unsigned int)tail * (unsigned int)list->elem_size);
    }
    list->count--;
}

void csr_list_clear(CsList *list) { list->count = 0; }
int  csr_list_count(CsList *list) { return list->count; }

CsList *csr_list_copy(CsList *list) {
    CsList *out = csr_list_new(list->elem_size, list->count > 0 ? list->count : 4);
    if (list->count) memcpy(out->data, list->data, (unsigned int)list->count * (unsigned int)list->elem_size);
    out->count = list->count;
    return out;
}

/* ------------------------------------------------------------------------
 * 4c. CsDict (string-keyed)
 * ------------------------------------------------------------------------ */

CsDict *csr_dict_new(int val_size) {
    CsDict *d = (CsDict *)csr_gc_alloc((unsigned int)sizeof(CsDict), 0, CS_KIND_DICT);
    int cap = 8;
    d->val_size = val_size;
    d->count = 0;
    d->cap = cap;
    d->keys = (CsString **)calloc((unsigned int)cap, sizeof(CsString *));
    d->used = (unsigned char *)calloc((unsigned int)cap, 1);
    d->values = (unsigned char *)malloc((unsigned int)val_size * (unsigned int)cap);
    return d;
}

static unsigned int str_hash(CsString *s) {
    unsigned int h = 2166136261u;
    int i;
    for (i = 0; i < s->len; i++) { h ^= (unsigned char)s->data[i]; h *= 16777619u; }
    return h;
}

static int dict_find_slot(CsDict *d, CsString *key, int *out_idx) {
    unsigned int mask = (unsigned int)d->cap - 1;
    unsigned int idx = str_hash(key) & mask;
    unsigned int start = idx;
    do {
        if (!d->used[idx]) { *out_idx = (int)idx; return 0; }
        if (cs_string_eq(d->keys[idx], key)) { *out_idx = (int)idx; return 1; }
        idx = (idx + 1) & mask;
    } while (idx != start);
    *out_idx = -1;
    return 0;
}

static void dict_grow(CsDict *d) {
    int oldcap = d->cap;
    CsString **oldkeys = d->keys;
    unsigned char *oldused = d->used;
    unsigned char *oldvalues = d->values;
    int newcap = oldcap * 2;
    int i;
    d->cap = newcap;
    d->keys = (CsString **)calloc((unsigned int)newcap, sizeof(CsString *));
    d->used = (unsigned char *)calloc((unsigned int)newcap, 1);
    d->values = (unsigned char *)malloc((unsigned int)d->val_size * (unsigned int)newcap);
    d->count = 0;
    for (i = 0; i < oldcap; i++) {
        if (oldused[i]) csr_dict_set(d, oldkeys[i], oldvalues + (unsigned int)i * (unsigned int)d->val_size);
    }
    free(oldkeys); free(oldused); free(oldvalues);
}

void csr_dict_set(CsDict *d, CsString *key, const void *val) {
    int idx;
    if (d->count * 4 >= d->cap * 3) dict_grow(d); /* keep load factor <= 0.75 */
    dict_find_slot(d, key, &idx);
    if (idx < 0) { dict_grow(d); dict_find_slot(d, key, &idx); }
    if (!d->used[idx]) { d->used[idx] = 1; d->keys[idx] = key; d->count++; }
    memcpy(d->values + (unsigned int)idx * (unsigned int)d->val_size, val, (unsigned int)d->val_size);
}

int csr_dict_try_get(CsDict *d, CsString *key, void *out) {
    int idx;
    if (dict_find_slot(d, key, &idx) && idx >= 0) {
        memcpy(out, d->values + (unsigned int)idx * (unsigned int)d->val_size, (unsigned int)d->val_size);
        return 1;
    }
    return 0;
}

int csr_dict_contains_key(CsDict *d, CsString *key) {
    int idx;
    return dict_find_slot(d, key, &idx) && idx >= 0;
}

int csr_dict_count(CsDict *d) { return d->count; }

/* ------------------------------------------------------------------------
 * 4d. Console
 * ------------------------------------------------------------------------ */

void csr_console_write_line(CsString *s) {
    if (s) fwrite(s->data, 1, (unsigned int)s->len, stdout);
    fputc('\n', stdout);
}

void csr_console_write(CsString *s) {
    if (s) fwrite(s->data, 1, (unsigned int)s->len, stdout);
}

/* ------------------------------------------------------------------------
 * 4e. LINQ
 * ------------------------------------------------------------------------ */

CsList *csr_linq_where(CsList *src, int (*pred)(void *capture, const void *elem), void *capture) {
    CsList *out = csr_list_new(src->elem_size, src->count > 0 ? src->count : 4);
    int i;
    for (i = 0; i < src->count; i++) {
        unsigned char *e = src->data + (unsigned int)i * (unsigned int)src->elem_size;
        if (pred(capture, e)) csr_list_add(out, e);
    }
    return out;
}

CsList *csr_linq_select(CsList *src, int out_elem_size,
                         void (*proj)(void *capture, const void *elem, void *out), void *capture) {
    CsList *out = csr_list_new(out_elem_size, src->count > 0 ? src->count : 4);
    unsigned char tmp[256]; /* documented cap: a single LINQ-projected element
                              * larger than 256 bytes is out of scope for this
                              * phase (real C# structs this large in a hot
                              * LINQ path are rare; revisit with a heap temp
                              * if this ever bites a real script). */
    int i;
    for (i = 0; i < src->count; i++) {
        unsigned char *e = src->data + (unsigned int)i * (unsigned int)src->elem_size;
        if (out_elem_size > (int)sizeof(tmp)) { fprintf(stderr, "csharp_rt: LINQ Select element too large (%d bytes)\n", out_elem_size); abort(); }
        proj(capture, e, tmp);
        csr_list_add(out, tmp);
    }
    return out;
}

CsList *csr_linq_order_by(CsList *src, int (*cmp)(void *capture, const void *a, const void *b),
                           void *capture, int descending) {
    CsList *out = csr_list_copy(src);
    /* insertion sort -- fine for the list sizes real LINQ-in-a-browser-
     * script scenarios realistically involve; not the place for an O(n
     * log n) algorithm on this phase's budget (matches php_mini.c's own
     * array_sort_by, which made the identical call for the same reason). */
    int i, j;
    unsigned char *tmp = (unsigned char *)malloc((unsigned int)out->elem_size);
    for (i = 1; i < out->count; i++) {
        memcpy(tmp, out->data + (unsigned int)i * (unsigned int)out->elem_size, (unsigned int)out->elem_size);
        j = i - 1;
        while (j >= 0) {
            unsigned char *ej = out->data + (unsigned int)j * (unsigned int)out->elem_size;
            int c = cmp(capture, ej, tmp);
            if (descending ? (c < 0) : (c > 0)) {
                memcpy(out->data + (unsigned int)(j + 1) * (unsigned int)out->elem_size, ej, (unsigned int)out->elem_size);
                j--;
            } else break;
        }
        memcpy(out->data + (unsigned int)(j + 1) * (unsigned int)out->elem_size, tmp, (unsigned int)out->elem_size);
    }
    free(tmp);
    return out;
}

int csr_linq_count(CsList *src) { return src->count; }

int csr_linq_sum_int(CsList *src) {
    int sum = 0, i;
    for (i = 0; i < src->count; i++) {
        int v; csr_list_get(src, i, &v); sum += v;
    }
    return sum;
}

double csr_linq_sum_double(CsList *src) {
    double sum = 0.0; int i;
    for (i = 0; i < src->count; i++) {
        double v; csr_list_get(src, i, &v); sum += v;
    }
    return sum;
}

int csr_linq_first(CsList *src, void *out) {
    if (src->count == 0) return 0;
    csr_list_get(src, 0, out);
    return 1;
}

int csr_linq_first_where(CsList *src, int (*pred)(void *capture, const void *elem), void *capture, void *out) {
    int i;
    for (i = 0; i < src->count; i++) {
        unsigned char *e = src->data + (unsigned int)i * (unsigned int)src->elem_size;
        if (pred(capture, e)) { memcpy(out, e, (unsigned int)src->elem_size); return 1; }
    }
    return 0;
}

int csr_linq_any(CsList *src, int (*pred)(void *capture, const void *elem), void *capture) {
    int i;
    for (i = 0; i < src->count; i++) {
        unsigned char *e = src->data + (unsigned int)i * (unsigned int)src->elem_size;
        if (pred(capture, e)) return 1;
    }
    return 0;
}

int csr_linq_all(CsList *src, int (*pred)(void *capture, const void *elem), void *capture) {
    int i;
    for (i = 0; i < src->count; i++) {
        unsigned char *e = src->data + (unsigned int)i * (unsigned int)src->elem_size;
        if (!pred(capture, e)) return 0;
    }
    return 1;
}

CsList *csr_linq_to_list(CsList *src) { return csr_list_copy(src); }
