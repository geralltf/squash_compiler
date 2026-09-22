#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "cpp_lower.h"

/* ============================ string buffer ============================ */
typedef struct { char *buf; int len, cap; } StrBuf;
static void sb_init(StrBuf *b) { b->cap = 4096; b->buf = malloc(b->cap); b->buf[0] = '\0'; b->len = 0; }
static void sb_ensure(StrBuf *b, int more) {
    if (b->len + more + 1 <= b->cap) return;
    while (b->len + more + 1 > b->cap) b->cap *= 2;
    b->buf = realloc(b->buf, b->cap);
}
static void sb_append(StrBuf *b, const char *s) {
    int n = (int)strlen(s);
    sb_ensure(b, n);
    memcpy(b->buf + b->len, s, n + 1);
    b->len += n;
}
static void sb_appendf(StrBuf *b, const char *fmt, ...) {
    char tmp[8192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    sb_append(b, tmp);
}

static int g_error_count = 0;
static void lower_error(int line, const char *msg) {
    fprintf(stderr, "cpp: line %d: lowering error: %s\n", line, msg);
    g_error_count++;
}
static void lower_errorf(int line, const char *fmt, ...) {
    char tmp[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    lower_error(line, tmp);
}

static char *xstrdup(const char *s) { return s ? cpp_strdup(s) : NULL; }
static char *catnew(const char *a, const char *b) {
    char *r = malloc(strlen(a) + strlen(b) + 1);
    strcpy(r, a); strcat(r, b);
    return r;
}
static char *catnew3(const char *a, const char *b, const char *c) {
    char *r = malloc(strlen(a) + strlen(b) + strlen(c) + 1);
    strcpy(r, a); strcat(r, b); strcat(r, c);
    return r;
}

/* ============================== LType =================================== */
typedef enum { LT_VOID, LT_INT, LT_DOUBLE, LT_CHAR, LT_BOOL, LT_CSTR, LT_STRING, LT_VECTOR, LT_CLASS, LT_UNKNOWN } LTKind;
typedef struct {
    LTKind kind;
    char  *class_name;      /* LT_CLASS: mangled struct name. LT_VECTOR: mangled elem "ctype" tag */
    struct LTypeBox *elem;  /* LT_VECTOR only */
    int    ptr_depth;
} LType;
typedef struct LTypeBox { LType t; } LTypeBox;

static LType lt_simple(LTKind k) { LType t; t.kind = k; t.class_name = NULL; t.elem = NULL; t.ptr_depth = 0; return t; }
static LType lt_class(const char *name, int ptr_depth) { LType t; t.kind = LT_CLASS; t.class_name = xstrdup(name); t.elem = NULL; t.ptr_depth = ptr_depth; return t; }

/* ============================== class table ============================= */
typedef struct { char *src_name; LType type; int is_static; } FieldInfo;
typedef struct { LType type; int is_ref; CppNode *default_value; } ParamSig;
typedef struct {
    char *src_name, *mangled;
    LType ret; int ret_is_class;
    ParamSig *params; int n_params;
    int is_static, is_virtual, is_const;
    char *op_name;
} MethodInfo;
typedef struct { char *mangled; ParamSig *params; int n_params; } CtorInfo;

typedef struct ClassInfo {
    char *name;                 /* mangled C struct name */
    char *src_name;
    int   is_struct;
    char *base_name;            /* mangled, or NULL */
    struct ClassInfo *base;
    int   declares_virtual;
    int   has_vtable;
    struct ClassInfo *vtable_root;
    FieldInfo  *fields;   int n_fields;
    MethodInfo *methods;  int n_methods;
    CtorInfo   *ctors;    int n_ctors;
    int   has_dtor; char *dtor_mangled; int dtor_is_virtual;
    int   is_template; char **tparams; int n_tparams; CppNode *raw_decl;
    int   emitted;               /* struct/typedef text already written */
} ClassInfo;

/* g_classes/g_funcs are arrays of POINTERS to individually-malloc'd
 * ClassInfo/FuncInfo structs, NOT arrays of the structs themselves — a
 * real, confirmed bug: with the structs stored inline, any ClassInfo
 * or FuncInfo pointer captured before a nested class_new()/func_new()
 * call (e.g.
 * emit_func_body holding `fi` across lowering ITS OWN body, which can
 * itself trigger a template instantiation's func_new() call — see
 * ensure_func_template_instantiated) went silently DANGLING the moment
 * that realloc() moved the backing array, since realloc is free to
 * return a different address. Reproduced directly: two different-typed
 * calls to the same function template ("maxval(3,7)" then
 * "maxval(3.5,1.5)") corrupted the FIRST instantiation's own FuncInfo out
 * from under an in-flight emit_func_body call for a COMPLETELY UNRELATED
 * function ("main" itself), which read back garbage for its own name and
 * silently printed the wrong function signature. Only the POINTER ARRAY
 * moves now; each individual struct's address is stable for the whole
 * compile once allocated. */
static ClassInfo **g_classes = NULL; static int g_n_classes = 0, g_cap_classes = 0;
static ClassInfo *class_new(void) {
    if (g_n_classes == g_cap_classes) { g_cap_classes = g_cap_classes ? g_cap_classes * 2 : 8; g_classes = realloc(g_classes, sizeof(ClassInfo*) * g_cap_classes); }
    ClassInfo *c = calloc(1, sizeof(ClassInfo));
    g_classes[g_n_classes++] = c;
    return c;
}
static ClassInfo *find_class(const char *mangled_name) {
    if (!mangled_name) return NULL;
    for (int i = 0; i < g_n_classes; i++) if (g_classes[i]->name && strcmp(g_classes[i]->name, mangled_name) == 0) return g_classes[i];
    return NULL;
}
static ClassInfo *find_class_by_src(const char *src_name) {
    for (int i = 0; i < g_n_classes; i++) if (!g_classes[i]->is_template && strcmp(g_classes[i]->src_name, src_name) == 0) return g_classes[i];
    return NULL;
}
static ClassInfo *find_template_by_src(const char *src_name) {
    for (int i = 0; i < g_n_classes; i++) if (g_classes[i]->is_template && strcmp(g_classes[i]->src_name, src_name) == 0) return g_classes[i];
    return NULL;
}

typedef struct { char *src_name, *mangled; LType ret; ParamSig *params; int n_params; int is_template; char **tparams; int n_tparams; CppNode *raw_decl; } FuncInfo;
static FuncInfo **g_funcs = NULL; static int g_n_funcs = 0, g_cap_funcs = 0;
static FuncInfo *func_new(void) {
    if (g_n_funcs == g_cap_funcs) { g_cap_funcs = g_cap_funcs ? g_cap_funcs * 2 : 8; g_funcs = realloc(g_funcs, sizeof(FuncInfo*) * g_cap_funcs); }
    FuncInfo *f = calloc(1, sizeof(FuncInfo));
    g_funcs[g_n_funcs++] = f;
    memset(f, 0, sizeof *f);
    return f;
}
static FuncInfo *find_func_template_by_src(const char *src_name) {
    for (int i = 0; i < g_n_funcs; i++) if (g_funcs[i]->is_template && strcmp(g_funcs[i]->src_name, src_name) == 0) return g_funcs[i];
    return NULL;
}

/* Output sections, filled in dependency order then concatenated once at
 * the end (typedefs/structs before the functions that use them; template
 * instantiations before their first use — see ensure_class_instantiated). */
static StrBuf g_out_types;
static StrBuf g_out_funcs;

/* ---- env (locals/params in scope while lowering one function body) ---- */
typedef struct { char *name; char *lookup_name; LType type; int is_ref; } EnvEntry;
typedef struct { EnvEntry *items; int n, cap; } EnvFrame;
typedef struct { EnvFrame *frames; int n, cap; } Env;
static void env_init(Env *e) { e->frames = NULL; e->n = 0; e->cap = 0; }
static void env_push(Env *e) {
    if (e->n == e->cap) { e->cap = e->cap ? e->cap * 2 : 4; e->frames = realloc(e->frames, sizeof(EnvFrame) * e->cap); }
    memset(&e->frames[e->n], 0, sizeof(EnvFrame));
    e->n++;
}
static void env_pop(Env *e) { e->n--; free(e->frames[e->n].items); }
/* `lookup_name` is what a CPP_IDENT in the source resolves against;
 * `c_name` is the text actually emitted for it in the generated C — they
 * differ only for parameters, whose C-facing spelling is a positional
 * "p0"/"p1"/... (see emit_method/emit_ctor/emit_func_body) to sidestep a
 * source parameter name colliding with a C keyword or a name this
 * lowering pass itself uses internally (e.g. a real C++ parameter
 * literally named "this" — not valid as a C identifier but perfectly
 * legal to still look up as "this" here, since CPP_THIS is a distinct
 * AST node from CPP_IDENT anyway; the real motivation is uniformity, not
 * that specific collision). */
static void env_add_named(Env *e, const char *lookup_name, const char *c_name, LType t, int is_ref) {
    EnvFrame *f = &e->frames[e->n - 1];
    if (f->n == f->cap) { f->cap = f->cap ? f->cap * 2 : 8; f->items = realloc(f->items, sizeof(EnvEntry) * f->cap); }
    f->items[f->n].name = xstrdup(c_name);
    f->items[f->n].lookup_name = xstrdup(lookup_name);
    f->items[f->n].type = t;
    f->items[f->n].is_ref = is_ref;
    f->n++;
}
static void env_add(Env *e, const char *name, LType t, int is_ref) { env_add_named(e, name, name, t, is_ref); }
static EnvEntry *env_lookup(Env *e, const char *name) {
    for (int i = e->n - 1; i >= 0; i--) {
        EnvFrame *f = &e->frames[i];
        for (int j = f->n - 1; j >= 0; j--) if (strcmp(f->items[j].lookup_name, name) == 0) return &f->items[j];
    }
    return NULL;
}

static ClassInfo *g_cur_class = NULL;   /* class context while lowering a method body, or NULL */
static int g_tmp_counter = 0;
static char *namespace_prefix(const char **stack, int n) {
    if (n == 0) return xstrdup("");
    StrBuf b; sb_init(&b);
    for (int i = 0; i < n; i++) { sb_append(&b, stack[i]); sb_append(&b, "__"); }
    return b.buf;
}

/* ============================ type resolution ============================ */
static LType ensure_vector_instantiated(LType elem, int line);
static void ensure_class_instantiated_members(ClassInfo *tmpl, CppType *arg, int line);

static const char *base_ctype_str(LType t);

/* Exact-word match only — a naive strstr(n,"int") here previously matched
 * any user class/struct name that merely CONTAINS "int" as a substring
 * (e.g. "Point" -> "Po-int"), silently misclassifying it as the builtin
 * int type instead of a class. Multi-word builtin combos ("unsigned int",
 * "long long") are already caught by the space check at this function's
 * only call site, so this only needs the single-word spellings. */
static int name_is_intlike(const char *n) {
    return !strcmp(n, "int") || !strcmp(n, "short") || !strcmp(n, "long") || !strcmp(n, "unsigned") || !strcmp(n, "signed");
}

/* Active template-parameter substitution ("T" -> a concrete CppType) while
 * lowering ONE monomorphized instantiation's cloned member declarations
 * (see ensure_class_instantiated_members/ensure_func_template_instantiated)
 * — only a single template type parameter is supported (cpp_lower.h's own
 * documented scope), so one slot is enough; NULL outside instantiation. */
static char    *g_tsub_name = NULL;
static CppType *g_tsub_type = NULL;

static LType ltype_of_cpptype(CppType *t, int line) {
    if (!t) return lt_simple(LT_VOID);
    if (g_tsub_name && !strcmp(t->name, g_tsub_name)) {
        CppType merged = *g_tsub_type;
        merged.ptr_depth = g_tsub_type->ptr_depth + t->ptr_depth;
        merged.is_ref = t->is_ref || g_tsub_type->is_ref;
        return ltype_of_cpptype(&merged, line);
    }
    if (!strcmp(t->name, "void")) { LType r = lt_simple(LT_VOID); r.ptr_depth = t->ptr_depth; return r; }
    if (!strcmp(t->name, "double") || !strcmp(t->name, "float")) { LType r = lt_simple(LT_DOUBLE); r.ptr_depth = t->ptr_depth; return r; }
    if (!strcmp(t->name, "bool")) { LType r = lt_simple(LT_BOOL); r.ptr_depth = t->ptr_depth; return r; }
    if (!strcmp(t->name, "char")) {
        LType r = lt_simple(t->ptr_depth > 0 ? LT_CSTR : LT_CHAR); r.ptr_depth = t->ptr_depth; return r;
    }
    if (!strcmp(t->name, "std::string") || !strcmp(t->name, "string")) { LType r = lt_simple(LT_STRING); r.class_name = xstrdup("CppString"); r.ptr_depth = t->ptr_depth; return r; }
    if (!strcmp(t->name, "std::vector")) {
        LType elem = t->n_type_args > 0 ? ltype_of_cpptype(t->type_args[0], line) : lt_simple(LT_INT);
        LType v = ensure_vector_instantiated(elem, line);
        v.ptr_depth = t->ptr_depth;
        return v;
    }
    if (name_is_intlike(t->name) || strchr(t->name, ' ')) { LType r = lt_simple(LT_INT); r.ptr_depth = t->ptr_depth; return r; }
    /* Otherwise: a user class/struct name, possibly a template instantiation
     * ("Stack<int>" — t->n_type_args > 0) or a plain class name. */
    ClassInfo *tmpl = find_template_by_src(t->name);
    if (tmpl && t->n_type_args > 0) {
        ensure_class_instantiated_members(tmpl, t->type_args[0], line);
        char *mangled = catnew3(t->name, "__", (t->type_args[0] ? t->type_args[0]->name : "int"));
        for (char *p = mangled; *p; p++) if (*p == ':') *p = '_';
        LType r = lt_class(mangled, t->ptr_depth);
        free(mangled);
        return r;
    }
    ClassInfo *ci = find_class_by_src(t->name);
    if (!ci && strchr(t->name, ':')) {
        /* A namespace-qualified type name ("geo::Vec2") — collect_class
         * only ever registers a class under its own BARE name (src_name),
         * never the qualified spelling, so find_class_by_src("geo::Vec2")
         * always missed here; a real, confirmed bug: any local variable
         * declared with its namespace-qualified type name (as opposed to
         * an unqualified use after "using namespace geo;") failed every
         * member-access lookup afterward, since its LType never resolved
         * to the real ClassInfo at all. "::" and the mangled-name "__"
         * join are the exact same separator collect_class already uses
         * for a namespaced class's own ->name (ns_prefix + bare name), so
         * a straight substitution finds it directly. */
        char *mangled = xstrdup(t->name);
        for (char *p = mangled; *p; p++) if (*p == ':') *p = '_';
        ci = find_class(mangled);
        free(mangled);
    }
    if (ci) { LType r = lt_class(ci->name, t->ptr_depth); return r; }
    /* Bare name not seen yet (e.g. forward use) — fall back to using the
     * source name unmangled; a real class of this exact name registered
     * later in the same (flat, non-namespaced) file will still line up. */
    LType r = lt_class(t->name, t->ptr_depth);
    return r;
}

static const char *base_ctype_str(LType t) {
    switch (t.kind) {
        case LT_VOID: return "void";
        case LT_INT: return "int";
        case LT_DOUBLE: return "double";
        case LT_CHAR: return "char";
        case LT_BOOL: return "int";
        case LT_CSTR: return "char";
        case LT_STRING: return "CppString";
        case LT_VECTOR: return t.class_name;   /* already the mangled vector struct name */
        case LT_CLASS: return t.class_name;
        default: return "int";
    }
}

/* Full C type text, e.g. "Point", "Point*", "int", "char*". */
static char *ctype_full(LType t) {
    StrBuf b; sb_init(&b);
    sb_append(&b, base_ctype_str(t));
    for (int i = 0; i < t.ptr_depth; i++) sb_append(&b, "*");
    return b.buf;
}

static int ltype_eq(LType a, LType b) {
    if (a.kind != b.kind) return 0;
    if (a.ptr_depth != b.ptr_depth) return 0;
    if (a.class_name && b.class_name) return strcmp(a.class_name, b.class_name) == 0;
    return a.class_name == b.class_name;
}

static int ltype_matches(LType want, LType got);
static char *vptr_path_from_instance(ClassInfo *ci);
static FuncInfo *ensure_func_template_instantiated(FuncInfo *tmpl, LType arg0, int line);

/* ========================= member resolution =========================== */
/* Finds `name` as a field or method in `ci` or an ancestor; `*path` gets a
 * dot-separated chain of "__base." hops to reach the class that actually
 * owns it (empty string if `ci` itself owns it) — see cpp_lower.h's own
 * comment on the first-member-embedding inheritance model this assumes. */
static FieldInfo *find_field(ClassInfo *ci, const char *name, char *path, int pathcap) {
    if (!ci) return NULL;
    for (int i = 0; i < ci->n_fields; i++) if (!strcmp(ci->fields[i].src_name, name)) { path[0] = '\0'; return &ci->fields[i]; }
    if (ci->base) {
        char sub[512];
        FieldInfo *f = find_field(ci->base, name, sub, sizeof sub);
        if (f) { snprintf(path, pathcap, "__base.%s", sub); return f; }
    }
    return NULL;
}
static LType infer_type(Env *env, CppNode *n);
/* `env`/`args` may be NULL when the caller has no argument expressions to
 * disambiguate with (e.g. an "operator" lookup that already knows exactly
 * which slot it wants) — every other caller passes them so a same-name,
 * same-argc overload set (see sig_suffix_from_params's own comment) picks
 * the right candidate instead of always the first one found. */
static MethodInfo *find_method_bucket(ClassInfo *ci, const char *name, Env *env, CppNode **args, int argc, char *path, int pathcap, ClassInfo **owner) {
    if (!ci) return NULL;
    MethodInfo *only = NULL; int only_count = 0;
    MethodInfo *argc_match = NULL; int argc_match_count = 0;
    for (int i = 0; i < ci->n_methods; i++) {
        if (strcmp(ci->methods[i].src_name, name) != 0) continue;
        only_count++; only = &ci->methods[i];
        if (ci->methods[i].n_params == argc) { argc_match_count++; argc_match = &ci->methods[i]; }
    }
    if (only_count == 1) { path[0] = '\0'; if (owner) *owner = ci; return only; }
    if (argc_match_count == 1) { path[0] = '\0'; if (owner) *owner = ci; return argc_match; }
    if (argc_match_count > 1) {
        if (env && args) {
            for (int i = 0; i < ci->n_methods; i++) {
                if (strcmp(ci->methods[i].src_name, name) != 0 || ci->methods[i].n_params != argc) continue;
                int all_match = 1;
                for (int k = 0; k < argc; k++) if (!ltype_matches(ci->methods[i].params[k].type, infer_type(env, args[k]))) { all_match = 0; break; }
                if (all_match) { path[0] = '\0'; if (owner) *owner = ci; return &ci->methods[i]; }
            }
        }
        path[0] = '\0'; if (owner) *owner = ci; return argc_match;
    }
    if (ci->base) {
        char sub[512];
        MethodInfo *m = find_method_bucket(ci->base, name, env, args, argc, sub, sizeof sub, owner);
        if (m) { snprintf(path, pathcap, "__base.%s", sub); return m; }
    }
    return NULL;
}

/* ============================= expr lowering ============================ */
static char *lower_expr(Env *env, CppNode *n, StrBuf *pre);
static char *lower_addr(Env *env, CppNode *n, StrBuf *pre);
static LType infer_type(Env *env, CppNode *n);
static void lower_stmt(Env *env, CppNode *n, StrBuf *out, int indent);
static char *ctor_call_for(ClassInfo *ci, const char *addr_expr, CppNode **args, int argc, Env *env, StrBuf *pre, int line);
static void emit_implicit_field_init(ClassInfo *ci, const char *addr_expr, StrBuf *out, int indent);
static void emit_indent(StrBuf *out, int indent) { for (int i = 0; i < indent; i++) sb_append(out, "    "); }
static char *fresh_tmp(void) { char buf[32]; snprintf(buf, sizeof buf, "__cpptmp%d", g_tmp_counter++); return xstrdup(buf); }

static int is_cout_marker(CppNode *n) { return n->kind == CPP_SCOPE && !strcmp(n->scope.qualifier, "std") && !strcmp(n->scope.name, "cout"); }
static int is_cin_marker(CppNode *n) { return n->kind == CPP_SCOPE && !strcmp(n->scope.qualifier, "std") && !strcmp(n->scope.name, "cin"); }
static int is_endl_marker(CppNode *n) { return n->kind == CPP_SCOPE && !strcmp(n->scope.qualifier, "std") && !strcmp(n->scope.name, "endl"); }

static int is_class_valued(LType t) { return t.kind == LT_CLASS && t.ptr_depth == 0; }

/* Same argc-then-type disambiguation as find_method_bucket, for free
 * (non-member) function overloads — see sig_suffix_from_params' own
 * comment for the bug this exists to avoid. */
static FuncInfo *find_func_overload(Env *env, const char *name, CppNode **args, int argc) {
    FuncInfo *only = NULL; int only_count = 0;
    FuncInfo *argc_match = NULL; int argc_match_count = 0;
    for (int i = 0; i < g_n_funcs; i++) {
        if (g_funcs[i]->is_template || strcmp(g_funcs[i]->src_name, name) != 0) continue;
        only_count++; only = g_funcs[i];
        if (g_funcs[i]->n_params == argc) { argc_match_count++; argc_match = g_funcs[i]; }
    }
    if (only_count == 1) return only;
    if (argc_match_count == 1) return argc_match;
    if (argc_match_count > 1 && env && args) {
        for (int i = 0; i < g_n_funcs; i++) {
            if (g_funcs[i]->is_template || strcmp(g_funcs[i]->src_name, name) != 0 || g_funcs[i]->n_params != argc) continue;
            int all_match = 1;
            for (int k = 0; k < argc; k++) if (!ltype_matches(g_funcs[i]->params[k].type, infer_type(env, args[k]))) { all_match = 0; break; }
            if (all_match) return g_funcs[i];
        }
    }
    if (argc_match) return argc_match;
    if (env && args && argc > 0) {
        for (int i = 0; i < g_n_funcs; i++) {
            if (!g_funcs[i]->is_template || strcmp(g_funcs[i]->src_name, name) != 0) continue;
            LType arg0 = infer_type(env, args[0]);
            return ensure_func_template_instantiated(g_funcs[i], arg0, args[0]->line);
        }
    }
    return NULL;
}

static LType infer_type(Env *env, CppNode *n) {
    if (!n) return lt_simple(LT_VOID);
    switch (n->kind) {
        case CPP_LIT_INT: return lt_simple(LT_INT);
        case CPP_LIT_DOUBLE: return lt_simple(LT_DOUBLE);
        case CPP_LIT_STRING: { LType t = lt_simple(LT_CSTR); t.ptr_depth = 1; return t; }
        case CPP_LIT_CHAR: return lt_simple(LT_CHAR);
        case CPP_LIT_BOOL: return lt_simple(LT_BOOL);
        case CPP_LIT_NULLPTR: { LType t = lt_simple(LT_UNKNOWN); t.ptr_depth = 1; return t; }
        case CPP_THIS: { LType t = lt_class(g_cur_class ? g_cur_class->name : "void", 1); return t; }
        case CPP_IDENT: {
            EnvEntry *e = env_lookup(env, n->ident.name);
            if (e) return e->type;
            FieldInfo *fi = g_cur_class ? &(FieldInfo){0} : NULL; (void)fi;
            char path[512];
            if (g_cur_class) { FieldInfo *f = find_field(g_cur_class, n->ident.name, path, sizeof path); if (f) return f->type; }
            return lt_simple(LT_INT);
        }
        case CPP_ASSIGN: return infer_type(env, n->assign.lhs);
        case CPP_UNARY:
            if (!strcmp(n->unary.op, "*")) { LType t = infer_type(env, n->unary.operand); if (t.ptr_depth > 0) t.ptr_depth--; return t; }
            if (!strcmp(n->unary.op, "&")) { LType t = infer_type(env, n->unary.operand); t.ptr_depth++; return t; }
            if (!strcmp(n->unary.op, "!")) return lt_simple(LT_BOOL);
            return infer_type(env, n->unary.operand);
        case CPP_TERNARY: return infer_type(env, n->ternary.then_);
        case CPP_BINARY: {
            if (!strcmp(n->binary.op, "==") || !strcmp(n->binary.op, "!=") || !strcmp(n->binary.op, "<") ||
                !strcmp(n->binary.op, ">") || !strcmp(n->binary.op, "<=") || !strcmp(n->binary.op, ">=") ||
                !strcmp(n->binary.op, "&&") || !strcmp(n->binary.op, "||"))
                return lt_simple(LT_BOOL);
            LType lt = infer_type(env, n->binary.left);
            LType rt = infer_type(env, n->binary.right);
            if (lt.kind == LT_STRING) return lt;
            if (is_class_valued(lt)) {
                ClassInfo *ci = find_class(lt.class_name);
                char path[512]; ClassInfo *owner = NULL;
                if (ci) {
                    for (int i = 0; i < ci->n_methods; i++) (void)i;
                    MethodInfo *m = NULL;
                    for (int i = 0; i < ci->n_methods; i++)
                        if (ci->methods[i].op_name && !strcmp(ci->methods[i].op_name, n->binary.op)) { m = &ci->methods[i]; break; }
                    if (m) return m->ret;
                    (void)path; (void)owner;
                }
                return lt;
            }
            if (lt.kind == LT_DOUBLE || rt.kind == LT_DOUBLE) return lt_simple(LT_DOUBLE);
            return lt_simple(LT_INT);
        }
        case CPP_CALL: {
            CppNode *callee = n->call.callee;
            if (callee->kind == CPP_IDENT) {
                FuncInfo *fi0 = find_func_overload(env, callee->ident.name, n->call.args, n->call.argc);
                if (fi0) return fi0->ret;
                if (g_cur_class) {
                    char path[512]; ClassInfo *owner = NULL;
                    MethodInfo *m = find_method_bucket(g_cur_class, callee->ident.name, env, n->call.args, n->call.argc, path, sizeof path, &owner);
                    if (m) return m->ret;
                }
                return lt_simple(LT_INT);
            }
            if (callee->kind == CPP_MEMBER || callee->kind == CPP_ARROW) {
                CppNode *obj = callee->kind == CPP_MEMBER ? callee->member.obj : callee->arrow.obj;
                const char *mname = callee->kind == CPP_MEMBER ? callee->member.name : callee->arrow.name;
                LType ot = infer_type(env, obj);
                if (ot.kind == LT_STRING) return lt_simple(LT_INT);
                if (ot.kind == LT_VECTOR) {
                    if (!strcmp(mname, "size")) return lt_simple(LT_INT);
                    if (!strcmp(mname, "push_back") || !strcmp(mname, "pop_back")) return lt_simple(LT_VOID);
                    if (!strcmp(mname, "at")) return ot.elem->t;
                    return lt_simple(LT_INT);
                }
                if (ot.kind == LT_CLASS) {
                    ClassInfo *ci = find_class(ot.class_name);
                    char path[512]; ClassInfo *owner = NULL;
                    MethodInfo *m = ci ? find_method_bucket(ci, mname, env, n->call.args, n->call.argc, path, sizeof path, &owner) : NULL;
                    if (m) return m->ret;
                }
                return lt_simple(LT_INT);
            }
            return lt_simple(LT_INT);
        }
        case CPP_MEMBER: case CPP_ARROW: {
            CppNode *obj = n->kind == CPP_MEMBER ? n->member.obj : n->arrow.obj;
            const char *mname = n->kind == CPP_MEMBER ? n->member.name : n->arrow.name;
            LType ot = infer_type(env, obj);
            if (ot.kind == LT_CLASS) {
                ClassInfo *ci = find_class(ot.class_name);
                char path[512];
                FieldInfo *f = ci ? find_field(ci, mname, path, sizeof path) : NULL;
                if (f) return f->type;
            }
            return lt_simple(LT_INT);
        }
        case CPP_INDEX: {
            LType ot = infer_type(env, n->index_.obj);
            if (ot.kind == LT_VECTOR) return ot.elem->t;
            if (ot.kind == LT_STRING) return lt_simple(LT_CHAR);
            LType r = ot; if (r.ptr_depth > 0) r.ptr_depth--; return r;
        }
        case CPP_NEW: {
            ClassInfo *ci = find_class_by_src(n->new_.type->name);
            LType t = ci ? ltype_of_cpptype(n->new_.type, n->line) : lt_simple(LT_INT);
            t.ptr_depth++;
            return t;
        }
        case CPP_NEW_ARRAY: { LType t = ltype_of_cpptype(n->new_array.type, n->line); t.ptr_depth++; return t; }
        case CPP_CAST: return ltype_of_cpptype(n->cast.type, n->line);
        case CPP_SCOPE: {
            ClassInfo *ci = find_class_by_src(n->scope.qualifier);
            if (ci) return lt_simple(LT_INT); /* static member fallback */
            return lt_simple(LT_INT);
        }
        case CPP_SIZEOF: return lt_simple(LT_INT);
        default: return lt_simple(LT_INT);
    }
}

/* --------------------------- vector instantiation ------------------------ */
typedef struct { char *elem_ctype; char *mangled; } VecInst;
static VecInst *g_vecs = NULL; static int g_n_vecs = 0, g_cap_vecs = 0;

static LType ensure_vector_instantiated(LType elem, int line) {
    char *ectype = ctype_full(elem);
    char mangle[128]; snprintf(mangle, sizeof mangle, "cpp_vector__%s", ectype);
    for (char *p = mangle; *p; p++) if (*p == '*' ) *p = 'p'; else if (*p==' ') *p='_';
    for (int i = 0; i < g_n_vecs; i++) {
        if (!strcmp(g_vecs[i].mangled, mangle)) {
            LType r; r.kind = LT_VECTOR; r.class_name = xstrdup(mangle); r.ptr_depth = 0;
            r.elem = malloc(sizeof(LTypeBox)); r.elem->t = elem;
            free(ectype);
            return r;
        }
    }
    if (g_n_vecs == g_cap_vecs) { g_cap_vecs = g_cap_vecs ? g_cap_vecs * 2 : 4; g_vecs = realloc(g_vecs, sizeof(VecInst) * g_cap_vecs); }
    g_vecs[g_n_vecs].elem_ctype = ectype;
    g_vecs[g_n_vecs].mangled = xstrdup(mangle);
    g_n_vecs++;

    sb_appendf(&g_out_types, "typedef struct { CppVecRaw raw; } %s;\n", mangle);
    sb_appendf(&g_out_funcs,
        "void %s__ctor(void *__this) { %s *t = (%s*)__this; cpp_vecraw_ctor(&t->raw, sizeof(%s)); }\n"
        "void %s__dtor(void *__this) { %s *t = (%s*)__this; cpp_vecraw_dtor(&t->raw); }\n"
        "void %s__push_back(void *__this, %s v) { %s *t = (%s*)__this; cpp_vecraw_push(&t->raw, &v); }\n"
        "int %s__size(void *__this) { %s *t = (%s*)__this; return cpp_vecraw_size(&t->raw); }\n"
        "%s *%s__at(void *__this, int idx) { %s *t = (%s*)__this; return (%s*)cpp_vecraw_at(&t->raw, idx); }\n"
        "void %s__pop_back(void *__this) { %s *t = (%s*)__this; cpp_vecraw_pop_back(&t->raw); }\n",
        mangle, mangle, mangle, ectype,
        mangle, mangle, mangle,
        mangle, ectype, mangle, mangle,
        mangle, mangle, mangle,
        ectype, mangle, mangle, mangle, ectype,
        mangle, mangle, mangle
    );
    LType r; r.kind = LT_VECTOR; r.class_name = xstrdup(mangle); r.ptr_depth = 0;
    r.elem = malloc(sizeof(LTypeBox)); r.elem->t = elem;
    (void)line;
    return r;
}

/* =============================== operators =============================== */
static const char *c_binop(const char *op) { return op; } /* every supported op text is already valid C */

/* ------------------------------ call lowering ----------------------------- */
static char *lower_call_args(Env *env, CppNode **args, int argc, ParamSig *sig, int n_sig, StrBuf *pre) {
    StrBuf b; sb_init(&b);
    for (int i = 0; i < argc; i++) {
        if (i > 0) sb_append(&b, ", ");
        int is_ref = (sig && i < n_sig) ? sig[i].is_ref : 0;
        if (is_ref) { char *a = lower_addr(env, args[i], pre); sb_append(&b, a); free(a); }
        else { char *a = lower_expr(env, args[i], pre); sb_append(&b, a); free(a); }
    }
    /* Default arguments: the caller passed fewer args than the callee
     * declares (argc < n_sig) — the generated C function always takes
     * every declared parameter (no C-level default-argument support), so
     * every trailing omitted parameter's OWN default-value expression
     * must be lowered fresh, right here, and appended — a real, confirmed
     * bug otherwise: the emitted call passed fewer arguments than the C
     * function's own signature requires, which is undefined behavior (the
     * callee reads whatever garbage happens to be in the missing
     * argument's register/stack slot) rather than a compile error, so it
     * silently "worked" and produced nonsense at runtime (reproduced
     * directly: "int greet(const char*, int times=2)" called as
     * "greet(name)" read a garbage iteration count instead of 2). */
    if (sig) {
        for (int i = argc; i < n_sig; i++) {
            if (i > 0) sb_append(&b, ", ");
            if (sig[i].default_value) {
                if (sig[i].is_ref) { char *a = lower_addr(env, sig[i].default_value, pre); sb_append(&b, a); free(a); }
                else { char *a = lower_expr(env, sig[i].default_value, pre); sb_append(&b, a); free(a); }
            } else {
                lower_errorf(sig[i].default_value ? sig[i].default_value->line : 0,
                             "missing argument %d and no default value declared for it", i + 1);
                sb_append(&b, "0");
            }
        }
    }
    return b.buf;
}

/* Lowers a call whose target returns a class BY VALUE (out-pointer
 * convention — see cpp_lower.h) into a hoisted "CLASS tmp; fn(this, &tmp,
 * args...);" statement in `pre`, returning "tmp" as the resulting
 * expression text so this composes for nested expressions.
 *
 * `extra_first_arg` (the "this" pointer, when calling a method/ctor) goes
 * BEFORE "&tmp", not after — matching emit_method's own signature order
 * ("void *__this" first, then "void *__out" — see its own comment), NOT
 * the free-function order (emit_func_body puts "void *__out" alone,
 * first, since there is no "this" at all). A real, confirmed bug: this
 * used to always put "&tmp" first regardless, which happened to match
 * the free-function case by coincidence (never actually exercised by a
 * class-returning free function in testing) but was silently backwards
 * for every class-returning METHOD/operator — e.g. "Vec2
 * operator+(Vec2)" — passing the freshly-hoisted temp as the callee's
 * OWN "this" and the real receiver object as the output-write target,
 * so the receiver's own fields got overwritten with zeros/garbage and
 * the intended result temp was never actually written to at all. */
static char *hoist_class_call(const char *fn_name, const char *extra_first_arg, const char *rest_args, LType ret, StrBuf *pre) {
    char *tmp = fresh_tmp();
    char *ctype = ctype_full(ret);
    sb_appendf(pre, "%s %s;\n", ctype, tmp);
    if (extra_first_arg && extra_first_arg[0])
        sb_appendf(pre, "%s(%s, &%s%s%s);\n", fn_name, extra_first_arg, tmp, rest_args[0] ? ", " : "", rest_args);
    else
        sb_appendf(pre, "%s(&%s%s%s);\n", fn_name, tmp, rest_args[0] ? ", " : "", rest_args);
    free(ctype);
    return tmp;
}

/* Builds the "reach a field on this object" prefix for an address
 * expression `oa` (as produced by lower_addr — either a genuine pointer
 * VALUE such as a parameter or "__this_typed", or a SYNTHESIZED "&(X)"
 * wrapper fabricated around a plain non-pointer local/global so the rest
 * of this file can treat every object uniformly as "an address"). Real,
 * confirmed squash codegen bug: "(&(X))->field" — an inline "&" applied
 * directly to a named value and immediately arrow-dereferenced in the
 * SAME expression, with no intermediate pointer variable in between —
 * silently reads/writes the FIRST field's offset no matter which field
 * is actually named, for every field but the first (reproduced directly,
 * isolated down to a two-field struct: "(&(p))->y" read back p.x's value
 * instead of p.y's, both as a plain read and inside an outer "&(...)"
 * taking its address). Going through an intermediate pointer variable
 * first (as every ctor/method already does via "__this_typed = (T*)
 * __this;") does NOT trip this — only the inline "&expr->field" shape
 * does. So: when `oa` is exactly that synthesized "&(X)" wrapper, this
 * strips it back down to "(X)." (plain dot access on the real value)
 * instead of "(X)->" (which would reintroduce the buggy pattern);
 * anything else (a genuine pointer expression) still gets "->" as
 * normal. Every member-access call site in this file must go through
 * this helper instead of hand-formatting "(%s)->" itself. */
static char *member_prefix(const char *oa) {
    size_t n = strlen(oa);
    if (n > 3 && oa[0] == '&' && oa[1] == '(' && oa[n - 1] == ')') {
        char *inner = malloc(n - 2);
        memcpy(inner, oa + 2, n - 3);
        inner[n - 3] = '\0';
        char *r = malloc(strlen(inner) + 4);
        sprintf(r, "(%s).", inner);
        free(inner);
        return r;
    }
    char *r = malloc(strlen(oa) + 4);
    sprintf(r, "(%s)->", oa);
    return r;
}

static char *lower_expr(Env *env, CppNode *n, StrBuf *pre) {
    char buf[256];
    switch (n->kind) {
        case CPP_LIT_INT: snprintf(buf, sizeof buf, "%lldLL", n->lit_int.value); return xstrdup(buf);
        case CPP_LIT_DOUBLE: snprintf(buf, sizeof buf, "%g", n->lit_double.value); return xstrdup(buf);
        case CPP_LIT_CHAR: snprintf(buf, sizeof buf, "%lld", n->lit_char.value); return xstrdup(buf);
        case CPP_LIT_BOOL: return xstrdup(n->lit_bool.value ? "1" : "0");
        case CPP_LIT_NULLPTR: return xstrdup("((void*)0)");
        case CPP_LIT_STRING: {
            StrBuf b; sb_init(&b); sb_append(&b, "\"");
            for (const char *p = n->lit_string.value; *p; p++) {
                if (*p == '"' || *p == '\\') { sb_append(&b, "\\"); char c[2] = {*p,0}; sb_append(&b, c); }
                else if (*p == '\n') sb_append(&b, "\\n");
                else { char c[2] = {*p,0}; sb_append(&b, c); }
            }
            sb_append(&b, "\"");
            return b.buf;
        }
        case CPP_THIS: return xstrdup("(*__this_typed)");
        case CPP_IDENT: {
            EnvEntry *e = env_lookup(env, n->ident.name);
            if (e) return e->is_ref ? catnew3("(*", e->name, ")") : xstrdup(e->name);
            if (g_cur_class) {
                char path[512];
                FieldInfo *f = find_field(g_cur_class, n->ident.name, path, sizeof path);
                if (f) { char tmp2[600]; snprintf(tmp2, sizeof tmp2, "__this_typed->%s%s", path, n->ident.name); return xstrdup(tmp2); }
            }
            return xstrdup(n->ident.name);
        }
        case CPP_SCOPE: {
            /* std::endl used outside a cout chain, or Class::staticMember */
            if (!strcmp(n->scope.qualifier, "std")) return xstrdup("0");
            char tmp2[512]; snprintf(tmp2, sizeof tmp2, "%s__%s", n->scope.qualifier, n->scope.name);
            return xstrdup(tmp2);
        }
        case CPP_ASSIGN: {
            LType lt = infer_type(env, n->assign.lhs);
            if (lt.kind == LT_STRING && !strcmp(n->assign.op, "=")) {
                char *la = lower_addr(env, n->assign.lhs, pre);
                LType rt = infer_type(env, n->assign.rhs);
                if (rt.kind == LT_CSTR) { char *ra = lower_expr(env, n->assign.rhs, pre); char *r = catnew3("(cpp_string_assign_cstr(", la, ""); StrBuf b; sb_init(&b); sb_appendf(&b, "(cpp_string_assign_cstr(%s, %s), %s)", la, ra, la); free(la); free(ra); free(r); return b.buf; }
                char *ra = lower_addr(env, n->assign.rhs, pre);
                StrBuf b; sb_init(&b); sb_appendf(&b, "(cpp_string_assign(%s, %s), %s)", la, ra, la);
                free(la); free(ra); return b.buf;
            }
            if (is_class_valued(lt) && !strcmp(n->assign.op, "=")) {
                /* operator= overloading not resolved here — plain field-for-
                 * field struct assignment, which squash's backend copies
                 * correctly (confirmed; only BY-VALUE RETURN is broken —
                 * see cpp_lower.h), covers ordinary value semantics fine. */
                char *la = lower_expr(env, n->assign.lhs, pre);
                char *ra = lower_expr(env, n->assign.rhs, pre);
                StrBuf b; sb_init(&b); sb_appendf(&b, "(%s = %s)", la, ra);
                free(la); free(ra); return b.buf;
            }
            char *la = lower_expr(env, n->assign.lhs, pre);
            char *ra = lower_expr(env, n->assign.rhs, pre);
            StrBuf b; sb_init(&b); sb_appendf(&b, "(%s %s %s)", la, n->assign.op, ra);
            free(la); free(ra); return b.buf;
        }
        case CPP_UNARY: {
            if (!strcmp(n->unary.op, "&")) { char *a = lower_addr(env, n->unary.operand, pre); return a; }
            char *o = lower_expr(env, n->unary.operand, pre);
            StrBuf b; sb_init(&b);
            if (n->unary.postfix) sb_appendf(&b, "(%s%s)", o, n->unary.op);
            else sb_appendf(&b, "(%s%s)", n->unary.op, o);
            free(o); return b.buf;
        }
        case CPP_TERNARY: {
            char *c = lower_expr(env, n->ternary.cond, pre);
            char *t = lower_expr(env, n->ternary.then_, pre);
            char *e = lower_expr(env, n->ternary.else_, pre);
            StrBuf b; sb_init(&b); sb_appendf(&b, "(%s ? %s : %s)", c, t, e);
            free(c); free(t); free(e); return b.buf;
        }
        case CPP_BINARY: {
            LType lt = infer_type(env, n->binary.left);
            if (lt.kind == LT_STRING) {
                if (!strcmp(n->binary.op, "+")) {
                    char *la = lower_addr(env, n->binary.left, pre);
                    LType rt = infer_type(env, n->binary.right);
                    char *tmp = fresh_tmp();
                    sb_appendf(pre, "CppString %s;\n", tmp);
                    if (rt.kind == LT_CSTR) { char *ra = lower_expr(env, n->binary.right, pre); sb_appendf(pre, "cpp_string_concat_cstr(&%s, %s, %s);\n", tmp, la, ra); free(ra); }
                    else { char *ra = lower_addr(env, n->binary.right, pre); sb_appendf(pre, "cpp_string_concat(&%s, %s, %s);\n", tmp, la, ra); free(ra); }
                    free(la); return tmp;
                }
                if (!strcmp(n->binary.op, "==") || !strcmp(n->binary.op, "!=")) {
                    char *la = lower_addr(env, n->binary.left, pre);
                    LType rt = infer_type(env, n->binary.right);
                    StrBuf b; sb_init(&b);
                    if (rt.kind == LT_CSTR) { char *ra = lower_expr(env, n->binary.right, pre); sb_appendf(&b, "(%scpp_string_eq_cstr(%s, %s))", !strcmp(n->binary.op,"!=") ? "!" : "", la, ra); free(ra); }
                    else { char *ra = lower_addr(env, n->binary.right, pre); sb_appendf(&b, "(%scpp_string_eq(%s, %s))", !strcmp(n->binary.op,"!=") ? "!" : "", la, ra); free(ra); }
                    free(la); return b.buf;
                }
            }
            if (is_class_valued(lt) && n->binary.op) {
                ClassInfo *ci = find_class(lt.class_name);
                MethodInfo *m = NULL;
                if (ci) for (int i = 0; i < ci->n_methods; i++) if (ci->methods[i].op_name && !strcmp(ci->methods[i].op_name, n->binary.op)) { m = &ci->methods[i]; break; }
                if (m) {
                    char *la = lower_addr(env, n->binary.left, pre);
                    CppNode *ra_args[1]; ra_args[0] = n->binary.right;
                    char *args = lower_call_args(env, ra_args, 1, m->params, m->n_params, pre);
                    if (is_class_valued(m->ret)) { char *r = hoist_class_call(m->mangled, la, args, m->ret, pre); free(la); free(args); return r; }
                    StrBuf b; sb_init(&b); sb_appendf(&b, "%s(%s, %s)", m->mangled, la, args);
                    free(la); free(args); return b.buf;
                }
            }
            char *la = lower_expr(env, n->binary.left, pre);
            char *ra = lower_expr(env, n->binary.right, pre);
            StrBuf b; sb_init(&b); sb_appendf(&b, "(%s %s %s)", la, c_binop(n->binary.op), ra);
            free(la); free(ra); return b.buf;
        }
        case CPP_CALL: {
            CppNode *callee = n->call.callee;
            /* std::cout / std::cin chains */
            if (callee == NULL) return xstrdup("0"); /* shouldn't happen for lower_expr */
            if (callee->kind == CPP_MEMBER || callee->kind == CPP_ARROW) {
                CppNode *obj = callee->kind == CPP_MEMBER ? callee->member.obj : callee->arrow.obj;
                const char *mname = callee->kind == CPP_MEMBER ? callee->member.name : callee->arrow.name;
                LType ot = infer_type(env, obj);
                if (ot.kind == LT_VECTOR) {
                    char *oa = lower_addr(env, obj, pre);
                    if (!strcmp(mname, "push_back")) {
                        char *v = lower_expr(env, n->call.args[0], pre);
                        StrBuf b; sb_init(&b); sb_appendf(&b, "%s__push_back(%s, %s)", ot.class_name, oa, v);
                        free(oa); free(v); return b.buf;
                    }
                    if (!strcmp(mname, "size")) { StrBuf b; sb_init(&b); sb_appendf(&b, "%s__size(%s)", ot.class_name, oa); free(oa); return b.buf; }
                    if (!strcmp(mname, "pop_back")) { StrBuf b; sb_init(&b); sb_appendf(&b, "%s__pop_back(%s)", ot.class_name, oa); free(oa); return b.buf; }
                    if (!strcmp(mname, "at")) {
                        char *idx = lower_expr(env, n->call.args[0], pre);
                        StrBuf b; sb_init(&b); sb_appendf(&b, "(*%s__at(%s, %s))", ot.class_name, oa, idx);
                        free(oa); free(idx); return b.buf;
                    }
                    free(oa);
                }
                if (ot.kind == LT_STRING) {
                    char *oa = lower_addr(env, obj, pre);
                    if (!strcmp(mname, "length") || !strcmp(mname, "size")) { StrBuf b; sb_init(&b); sb_appendf(&b, "cpp_string_length(%s)", oa); free(oa); return b.buf; }
                    if (!strcmp(mname, "c_str")) { StrBuf b; sb_init(&b); sb_appendf(&b, "cpp_string_c_str(%s)", oa); free(oa); return b.buf; }
                    if (!strcmp(mname, "at")) { char *idx = lower_expr(env, n->call.args[0], pre); StrBuf b; sb_init(&b); sb_appendf(&b, "cpp_string_at(%s, %s)", oa, idx); free(oa); free(idx); return b.buf; }
                    free(oa);
                }
                if (ot.kind == LT_CLASS) {
                    ClassInfo *ci = find_class(ot.class_name);
                    if (!ci) { lower_errorf(n->line, "call to unknown type '%s'", ot.class_name); return xstrdup("0"); }
                    char path[512]; ClassInfo *owner = NULL;
                    MethodInfo *m = find_method_bucket(ci, mname, env, n->call.args, n->call.argc, path, sizeof path, &owner);
                    if (!m) { lower_errorf(n->line, "no method '%s' with %d argument(s) on class '%s'", mname, n->call.argc, ci->src_name); return xstrdup("0"); }
                    char *oa = m->is_static ? NULL : lower_addr(env, obj, pre);
                    char *fullpath = NULL;
                    if (oa && path[0]) { char *mp = member_prefix(oa); char tmp3[700]; snprintf(tmp3, sizeof tmp3, "&(%s%s)", mp, path); fullpath = xstrdup(tmp3); free(mp); }
                    char *args = lower_call_args(env, n->call.args, n->call.argc, m->params, m->n_params, pre);
                    StrBuf b; sb_init(&b);
                    const char *this_arg = fullpath ? fullpath : oa;
                    /* Virtual dispatch: call through `this_arg`'s OWN runtime
                     * vtable pointer (reached via ci's — the STATIC type's —
                     * own dot-path down to the vtable root, since ci itself
                     * may not be the root) rather than m->mangled directly,
                     * so a Square/Circle stored behind a Shape* still picks
                     * the override that actually ran in that object's own
                     * constructor. */
                    char *call_target = NULL;
                    if (m->is_virtual && this_arg) {
                        char *vpath = vptr_path_from_instance(ci);
                        char *mp = member_prefix(this_arg);
                        char tmp4[900];
                        snprintf(tmp4, sizeof tmp4, "(%s%s__vptr->%s)", mp, vpath, m->src_name);
                        call_target = xstrdup(tmp4);
                        free(vpath); free(mp);
                    } else call_target = xstrdup(m->mangled);
                    if (is_class_valued(m->ret)) {
                        char *r = hoist_class_call(call_target, this_arg ? this_arg : "", args, m->ret, pre);
                        free(oa); free(fullpath); free(args); free(call_target); return r;
                    }
                    if (this_arg) sb_appendf(&b, "%s(%s%s%s)", call_target, this_arg, args[0] ? ", " : "", args);
                    else sb_appendf(&b, "%s(%s)", call_target, args);
                    free(oa); free(fullpath); free(args); free(call_target); return b.buf;
                }
            }
            if (callee->kind == CPP_IDENT) {
                const char *name = callee->ident.name;
                /* free function (possibly a call to a template's instantiation, resolved by
                 * arg-count/return-inference not attempted for free templates beyond a
                 * single supported pattern — see try_instantiate_func_template below). */
                FuncInfo *fi = find_func_overload(env, name, n->call.args, n->call.argc);
                if (fi) {
                    char *args = lower_call_args(env, n->call.args, n->call.argc, fi->params, fi->n_params, pre);
                    if (is_class_valued(fi->ret)) { char *r = hoist_class_call(fi->mangled, NULL, args, fi->ret, pre); free(args); return r; }
                    StrBuf b; sb_init(&b); sb_appendf(&b, "%s(%s)", fi->mangled, args);
                    free(args); return b.buf;
                }
                if (g_cur_class) {
                    char path[512]; ClassInfo *owner = NULL;
                    MethodInfo *m = find_method_bucket(g_cur_class, name, env, n->call.args, n->call.argc, path, sizeof path, &owner);
                    if (m) {
                        char *args = lower_call_args(env, n->call.args, n->call.argc, m->params, m->n_params, pre);
                        char thisexpr[700]; snprintf(thisexpr, sizeof thisexpr, "(&(__this_typed->%s*(void*)0? *__this_typed:*__this_typed))", "");
                        char taddr[700]; snprintf(taddr, sizeof taddr, "&(__this_typed->%s)", path[0] ? path : "");
                        char *this_arg = path[0] ? xstrdup(taddr) : xstrdup("__this_typed");
                        if (is_class_valued(m->ret)) { char *r = hoist_class_call(m->mangled, this_arg, args, m->ret, pre); free(args); free(this_arg); return r; }
                        StrBuf b; sb_init(&b); sb_appendf(&b, "%s(%s%s%s)", m->mangled, this_arg, args[0] ? ", " : "", args);
                        free(args); free(this_arg); return b.buf;
                    }
                }
                /* Unknown — pass through verbatim (lets native/libc calls like
                 * "printf(...)" through an included real header work untouched). */
                char *args = lower_call_args(env, n->call.args, n->call.argc, NULL, 0, pre);
                StrBuf b; sb_init(&b); sb_appendf(&b, "%s(%s)", name, args);
                free(args); return b.buf;
            }
            char *ce = lower_expr(env, callee, pre);
            char *args = lower_call_args(env, n->call.args, n->call.argc, NULL, 0, pre);
            StrBuf b; sb_init(&b); sb_appendf(&b, "%s(%s)", ce, args);
            free(ce); free(args); return b.buf;
        }
        case CPP_MEMBER: case CPP_ARROW: {
            CppNode *obj = n->kind == CPP_MEMBER ? n->member.obj : n->arrow.obj;
            const char *mname = n->kind == CPP_MEMBER ? n->member.name : n->arrow.name;
            LType ot = infer_type(env, obj);
            if (ot.kind == LT_VECTOR && !strcmp(mname, "size")) { char *oa = lower_addr(env, obj, pre); StrBuf b; sb_init(&b); sb_appendf(&b, "%s__size(%s)", ot.class_name, oa); free(oa); return b.buf; }
            if (ot.kind == LT_STRING && (!strcmp(mname, "length") || !strcmp(mname, "size"))) { char *oa = lower_addr(env, obj, pre); StrBuf b; sb_init(&b); sb_appendf(&b, "cpp_string_length(%s)", oa); free(oa); return b.buf; }
            if (ot.kind == LT_CLASS) {
                ClassInfo *ci = find_class(ot.class_name);
                char path[512];
                FieldInfo *f = ci ? find_field(ci, mname, path, sizeof path) : NULL;
                if (f) {
                    char *oa = lower_addr(env, obj, pre);
                    char *mp = member_prefix(oa);
                    char tmp2[700]; snprintf(tmp2, sizeof tmp2, "%s%s%s", mp, path, mname);
                    free(oa); free(mp);
                    return xstrdup(tmp2);
                }
            }
            lower_errorf(n->line, "no field '%s' on this type", mname);
            return xstrdup("0");
        }
        case CPP_INDEX: {
            LType ot = infer_type(env, n->index_.obj);
            char *idx = lower_expr(env, n->index_.index, pre);
            if (ot.kind == LT_VECTOR) {
                char *oa = lower_addr(env, n->index_.obj, pre);
                StrBuf b; sb_init(&b); sb_appendf(&b, "(*%s__at(%s, %s))", ot.class_name, oa, idx);
                free(oa); free(idx); return b.buf;
            }
            char *oa = lower_expr(env, n->index_.obj, pre);
            StrBuf b; sb_init(&b); sb_appendf(&b, "(%s[%s])", oa, idx);
            free(oa); free(idx); return b.buf;
        }
        case CPP_NEW: {
            LType t = ltype_of_cpptype(n->new_.type, n->line);
            char *ctype = ctype_full(t);
            ClassInfo *ci = find_class(t.class_name);
            char *tmp = fresh_tmp();
            sb_appendf(pre, "%s *%s = (%s*)malloc(sizeof(%s));\n", ctype, tmp, ctype, ctype);
            if (ci && ci->n_ctors > 0) { char *cc = ctor_call_for(ci, tmp, n->new_.args, n->new_.argc, env, pre, n->line); if (cc) { sb_appendf(pre, "%s;\n", cc); free(cc); } }
            else if (ci) { char tmpaddr[300]; snprintf(tmpaddr, sizeof tmpaddr, "(%s)", tmp); emit_implicit_field_init(ci, tmpaddr, pre, 0); }
            else if (n->new_.argc == 1) { char *v = lower_expr(env, n->new_.args[0], pre); sb_appendf(pre, "*%s = %s;\n", tmp, v); free(v); }
            free(ctype);
            return tmp;
        }
        case CPP_NEW_ARRAY: {
            LType t = ltype_of_cpptype(n->new_array.type, n->line);
            char *ctype = ctype_full(t);
            char *sz = lower_expr(env, n->new_array.size_expr, pre);
            char *tmp = fresh_tmp();
            sb_appendf(pre, "%s *%s = (%s*)malloc(sizeof(%s) * (int)(%s));\n", ctype, tmp, ctype, ctype, sz);
            free(ctype); free(sz);
            return tmp;
        }
        case CPP_CAST: {
            LType t = ltype_of_cpptype(n->cast.type, n->line);
            char *ctype = ctype_full(t);
            char *e = lower_expr(env, n->cast.expr, pre);
            StrBuf b; sb_init(&b); sb_appendf(&b, "((%s)%s)", ctype, e);
            free(ctype); free(e); return b.buf;
        }
        case CPP_SIZEOF: {
            if (n->sizeof_.type) { LType t = ltype_of_cpptype(n->sizeof_.type, n->line); char *ctype = ctype_full(t); StrBuf b; sb_init(&b); sb_appendf(&b, "((int)sizeof(%s))", ctype); free(ctype); return b.buf; }
            char *e = lower_expr(env, n->sizeof_.expr, pre);
            StrBuf b; sb_init(&b); sb_appendf(&b, "((int)sizeof(%s))", e);
            free(e); return b.buf;
        }
        default:
            lower_errorf(n->line, "internal: no expression lowering for node kind %d", n->kind);
            return xstrdup("0");
    }
}

/* Address-of an lvalue expression — see cpp_lower.h's design note on why
 * every method/ctor call needs a pointer "this" regardless of whether the
 * receiver is itself a pointer, a reference, or a plain value. */
static char *lower_addr(Env *env, CppNode *n, StrBuf *pre) {
    if (n->kind == CPP_THIS) return xstrdup("__this_typed");
    if (n->kind == CPP_IDENT) {
        EnvEntry *e = env_lookup(env, n->ident.name);
        if (e) {
            if (e->is_ref) return xstrdup(e->name);
            if (e->type.ptr_depth > 0) return xstrdup(e->name);
            char tmp[300]; snprintf(tmp, sizeof tmp, "&(%s)", e->name);
            return xstrdup(tmp);
        }
        if (g_cur_class) {
            char path[512];
            FieldInfo *f = find_field(g_cur_class, n->ident.name, path, sizeof path);
            if (f) {
                char tmp2[700];
                if (f->type.ptr_depth > 0) snprintf(tmp2, sizeof tmp2, "__this_typed->%s%s", path, n->ident.name);
                else snprintf(tmp2, sizeof tmp2, "&(__this_typed->%s%s)", path, n->ident.name);
                return xstrdup(tmp2);
            }
        }
        char tmp[300]; snprintf(tmp, sizeof tmp, "&(%s)", n->ident.name); return xstrdup(tmp);
    }
    if (n->kind == CPP_MEMBER || n->kind == CPP_ARROW) {
        CppNode *obj = n->kind == CPP_MEMBER ? n->member.obj : n->arrow.obj;
        const char *mname = n->kind == CPP_MEMBER ? n->member.name : n->arrow.name;
        LType ot = infer_type(env, obj);
        if (ot.kind == LT_CLASS) {
            ClassInfo *ci = find_class(ot.class_name);
            char path[512];
            FieldInfo *f = ci ? find_field(ci, mname, path, sizeof path) : NULL;
            if (f) {
                char *oa = lower_addr(env, obj, pre);
                char *mp = member_prefix(oa);
                char tmp2[700];
                if (f->type.ptr_depth > 0) snprintf(tmp2, sizeof tmp2, "(%s%s%s)", mp, path, mname);
                else snprintf(tmp2, sizeof tmp2, "&(%s%s%s)", mp, path, mname);
                free(oa); free(mp);
                return xstrdup(tmp2);
            }
        }
    }
    if (n->kind == CPP_UNARY && !strcmp(n->unary.op, "*")) return lower_expr(env, n->unary.operand, pre);
    if (n->kind == CPP_INDEX) {
        LType ot = infer_type(env, n->index_.obj);
        char *idx = lower_expr(env, n->index_.index, pre);
        if (ot.kind == LT_VECTOR) {
            char *oa = lower_addr(env, n->index_.obj, pre);
            char tmp2[700]; snprintf(tmp2, sizeof tmp2, "%s__at(%s, %s)", ot.class_name, oa, idx);
            free(oa); free(idx); return xstrdup(tmp2);
        }
        char *oa = lower_expr(env, n->index_.obj, pre);
        char tmp2[700]; snprintf(tmp2, sizeof tmp2, "(%s + %s)", oa, idx);
        free(oa); free(idx); return xstrdup(tmp2);
    }
    char *v = lower_expr(env, n, pre);
    char tmp[600]; snprintf(tmp, sizeof tmp, "&(%s)", v);
    free(v);
    return xstrdup(tmp);
}

/* ============================= cout/cin chains ============================ */
static int cout_chain_collect(CppNode *n, CppNode **out, int *cnt, int cap) {
    if (n->kind == CPP_BINARY && !strcmp(n->binary.op, "<<")) {
        if (!cout_chain_collect(n->binary.left, out, cnt, cap)) return 0;
        if (*cnt >= cap) return 0;
        out[(*cnt)++] = n->binary.right;
        return 1;
    }
    if (is_cout_marker(n)) return 1;
    return 0;
}
static int cin_chain_collect(CppNode *n, CppNode **out, int *cnt, int cap) {
    if (n->kind == CPP_BINARY && !strcmp(n->binary.op, ">>")) {
        if (!cin_chain_collect(n->binary.left, out, cnt, cap)) return 0;
        if (*cnt >= cap) return 0;
        out[(*cnt)++] = n->binary.right;
        return 1;
    }
    if (is_cin_marker(n)) return 1;
    return 0;
}

static void lower_cout_operand(Env *env, CppNode *op, StrBuf *out, int indent) {
    if (is_endl_marker(op)) { emit_indent(out, indent); sb_append(out, "cpp_cout_endl();\n"); return; }
    StrBuf pre; sb_init(&pre);
    LType t = infer_type(env, op);
    char *e = lower_expr(env, op, &pre);
    emit_indent(out, indent); sb_append(out, pre.buf);
    emit_indent(out, indent);
    switch (t.kind) {
        case LT_DOUBLE: sb_appendf(out, "cpp_cout_double(%s);\n", e); break;
        case LT_CHAR: sb_appendf(out, "cpp_cout_char(%s);\n", e); break;
        case LT_CSTR: sb_appendf(out, "cpp_cout_cstr(%s);\n", e); break;
        case LT_BOOL: sb_appendf(out, "cpp_cout_bool(%s);\n", e); break;
        case LT_STRING: { char *a = lower_addr(env, op, &pre); sb_appendf(out, "cpp_cout_string(%s);\n", a); free(a); break; }
        default: sb_appendf(out, "cpp_cout_int((long long)(%s));\n", e); break;
    }
    free(e); free(pre.buf);
}
static void lower_cin_operand(Env *env, CppNode *op, StrBuf *out, int indent) {
    StrBuf pre; sb_init(&pre);
    LType t = infer_type(env, op);
    char *a = lower_addr(env, op, &pre);
    emit_indent(out, indent); sb_append(out, pre.buf);
    emit_indent(out, indent);
    if (t.kind == LT_DOUBLE) sb_appendf(out, "cpp_cin_double(%s);\n", a);
    else if (t.kind == LT_STRING) sb_appendf(out, "cpp_cin_string(%s);\n", a);
    else sb_appendf(out, "cpp_cin_int((long long*)(void*)%s);\n", a);
    free(a); free(pre.buf);
}

/* ============================= ctor helper ================================
 * Picks a constructor by argc, then (if more than one ctor shares that
 * argc — see sig_suffix_from_params' own comment on this class of
 * overload-ambiguity bug) by matching each parameter's broad type
 * category against the actual argument expressions' inferred types. */
static char *ctor_call_for(ClassInfo *ci, const char *addr_expr, CppNode **args, int argc, Env *env, StrBuf *pre, int line) {
    if (ci->n_ctors == 0) return NULL;
    CtorInfo *c = NULL;
    int argc_match_count = 0;
    for (int i = 0; i < ci->n_ctors; i++) if (ci->ctors[i].n_params == argc) { argc_match_count++; c = &ci->ctors[i]; }
    if (argc_match_count > 1 && env && args) {
        for (int i = 0; i < ci->n_ctors; i++) {
            if (ci->ctors[i].n_params != argc) continue;
            int all_match = 1;
            for (int k = 0; k < argc; k++) if (!ltype_matches(ci->ctors[i].params[k].type, infer_type(env, args[k]))) { all_match = 0; break; }
            if (all_match) { c = &ci->ctors[i]; break; }
        }
    }
    if (!c && ci->n_ctors == 1) c = &ci->ctors[0];
    if (!c) { lower_errorf(line, "no matching constructor for '%s' with %d argument(s)", ci->src_name, argc); return NULL; }
    char *cargs = lower_call_args(env, args, argc, c->params, c->n_params, pre);
    StrBuf b; sb_init(&b);
    sb_appendf(&b, "%s(%s%s%s)", c->mangled, addr_expr, cargs[0] ? ", " : "", cargs);
    free(cargs);
    return b.buf;
}

/* Emits implicit default-construction for every field of `ci` (and,
 * recursively, its base) that needs it — a std::string or std::vector<T>
 * field, or a nested class field whose own class has no explicit
 * constructors either. Only used when `ci` ITSELF has no explicit
 * constructor (see its call site) — a real, confirmed bug otherwise: real
 * C++ always default-constructs member fields regardless of whether the
 * owning class has a constructor, but a class with an explicit
 * constructor is left to construct its own non-trivial fields (matching
 * cpp_lower.h's documented scope: no member-initializer-list support), so
 * this only covers the no-ctor-at-all case, e.g. "Stack<int> st;" where
 * Stack's only field is a std::vector<int> that otherwise stayed pure
 * garbage stack memory — reproduced directly: the very first push_back()
 * on such an uninitialized vector segfaulted, since its "cap"/"data"
 * fields were never set to the safe empty-vector state cpp_vecraw_ctor()
 * establishes. */
static void emit_implicit_field_init(ClassInfo *ci, const char *addr_expr, StrBuf *out, int indent) {
    char *mp0 = member_prefix(addr_expr);
    if (ci->base && ci->base->n_ctors == 0) {
        char baddr[700]; snprintf(baddr, sizeof baddr, "(&(%s__base))", mp0);
        emit_implicit_field_init(ci->base, baddr, out, indent);
    }
    for (int i = 0; i < ci->n_fields; i++) {
        if (ci->fields[i].is_static) continue;
        LType t = ci->fields[i].type;
        char faddr[700];
        if (t.kind == LT_STRING) {
            snprintf(faddr, sizeof faddr, "(&(%s%s))", mp0, ci->fields[i].src_name);
            emit_indent(out, indent); sb_appendf(out, "cpp_string_ctor(%s);\n", faddr);
        } else if (t.kind == LT_VECTOR) {
            snprintf(faddr, sizeof faddr, "(&(%s%s))", mp0, ci->fields[i].src_name);
            emit_indent(out, indent); sb_appendf(out, "%s__ctor(%s);\n", t.class_name, faddr);
        } else if (is_class_valued(t)) {
            ClassInfo *fci = find_class(t.class_name);
            if (fci && fci->n_ctors == 0) {
                snprintf(faddr, sizeof faddr, "(&(%s%s))", mp0, ci->fields[i].src_name);
                emit_implicit_field_init(fci, faddr, out, indent);
            }
        }
    }
    free(mp0);
}

/* ============================== statements ================================ */
static void lower_stmt(Env *env, CppNode *n, StrBuf *out, int indent) {
    if (!n) return;
    switch (n->kind) {
        case CPP_BLOCK:
            env_push(env);
            emit_indent(out, indent); sb_append(out, "{\n");
            for (int i = 0; i < n->block.n_stmts; i++) lower_stmt(env, n->block.stmts[i], out, indent + 1);
            emit_indent(out, indent); sb_append(out, "}\n");
            env_pop(env);
            return;
        case CPP_IF: {
            StrBuf pre; sb_init(&pre);
            char *c = lower_expr(env, n->if_.cond, &pre);
            emit_indent(out, indent); sb_append(out, pre.buf);
            emit_indent(out, indent); sb_appendf(out, "if (%s)\n", c);
            free(c); free(pre.buf);
            lower_stmt(env, n->if_.then_, out, indent);
            if (n->if_.else_) { emit_indent(out, indent); sb_append(out, "else\n"); lower_stmt(env, n->if_.else_, out, indent); }
            return;
        }
        case CPP_WHILE: {
            /* A cond with side-effecting hoists is re-evaluated correctly by
             * turning "while (cond)" into "while (1) { <pre>; if (!(cond)) break; body }"
             * only when hoisting is actually needed — the common case (no
             * hoist) stays a plain, readable "while (cond)". */
            StrBuf pre; sb_init(&pre);
            char *c = lower_expr(env, n->while_.cond, &pre);
            if (pre.len == 0) {
                emit_indent(out, indent); sb_appendf(out, "while (%s)\n", c);
                lower_stmt(env, n->while_.body, out, indent);
            } else {
                emit_indent(out, indent); sb_append(out, "for (;;) {\n");
                emit_indent(out, indent + 1); sb_append(out, pre.buf);
                emit_indent(out, indent + 1); sb_appendf(out, "if (!(%s)) break;\n", c);
                lower_stmt(env, n->while_.body, out, indent + 1);
                emit_indent(out, indent); sb_append(out, "}\n");
            }
            free(c); free(pre.buf);
            return;
        }
        case CPP_DO_WHILE: {
            emit_indent(out, indent); sb_append(out, "do\n");
            lower_stmt(env, n->do_while.body, out, indent);
            StrBuf pre; sb_init(&pre);
            char *c = lower_expr(env, n->do_while.cond, &pre);
            emit_indent(out, indent); sb_appendf(out, "while (%s);\n", c);
            free(c); free(pre.buf);
            return;
        }
        case CPP_SWITCH: {
            StrBuf pre; sb_init(&pre);
            char *c = lower_expr(env, n->switch_.expr, &pre);
            emit_indent(out, indent); sb_append(out, pre.buf);
            emit_indent(out, indent); sb_appendf(out, "switch (%s) {\n", c);
            free(c); free(pre.buf);
            env_push(env);
            for (int i = 0; i < n->switch_.n_cases; i++) {
                CppNode *cs = n->switch_.cases[i];
                if (cs->switch_case.value) {
                    /* Case labels must be compile-time integer constants in
                     * C, exactly as in C++ — this lowering only supports a
                     * bare literal (or an already-lowerable constant
                     * expression) here, not a hoisted/temp-needing one. */
                    StrBuf vpre; sb_init(&vpre);
                    char *v = lower_expr(env, cs->switch_case.value, &vpre);
                    emit_indent(out, indent); sb_appendf(out, "case %s:\n", v);
                    free(v); free(vpre.buf);
                } else {
                    emit_indent(out, indent); sb_append(out, "default:\n");
                }
                for (int k = 0; k < cs->switch_case.n_stmts; k++) lower_stmt(env, cs->switch_case.stmts[k], out, indent + 1);
            }
            env_pop(env);
            emit_indent(out, indent); sb_append(out, "}\n");
            return;
        }
        case CPP_FOR: {
            env_push(env);
            emit_indent(out, indent); sb_append(out, "{\n");
            if (n->for_.init) lower_stmt(env, n->for_.init, out, indent + 1);
            StrBuf pre; sb_init(&pre);
            char *c = n->for_.cond ? lower_expr(env, n->for_.cond, &pre) : xstrdup("1");
            StrBuf stepb; sb_init(&stepb);
            char *step = n->for_.step ? lower_expr(env, n->for_.step, &stepb) : xstrdup("");
            if (pre.len == 0 && stepb.len == 0) {
                emit_indent(out, indent + 1); sb_appendf(out, "for (; %s; %s) {\n", c, step);
                for (int i = 0; i < (n->for_.body->kind == CPP_BLOCK ? n->for_.body->block.n_stmts : 0); i++)
                    lower_stmt(env, n->for_.body->block.stmts[i], out, indent + 2);
                if (n->for_.body->kind != CPP_BLOCK) lower_stmt(env, n->for_.body, out, indent + 2);
                emit_indent(out, indent + 1); sb_append(out, "}\n");
            } else {
                emit_indent(out, indent + 1); sb_append(out, "for (;;) {\n");
                emit_indent(out, indent + 2); sb_append(out, pre.buf);
                emit_indent(out, indent + 2); sb_appendf(out, "if (!(%s)) break;\n", c);
                lower_stmt(env, n->for_.body, out, indent + 2);
                emit_indent(out, indent + 2); sb_append(out, stepb.buf);
                emit_indent(out, indent + 1); sb_append(out, "}\n");
            }
            free(c); free(step); free(pre.buf); free(stepb.buf);
            emit_indent(out, indent); sb_append(out, "}\n");
            env_pop(env);
            return;
        }
        case CPP_BREAK: emit_indent(out, indent); sb_append(out, "break;\n"); return;
        case CPP_CONTINUE: emit_indent(out, indent); sb_append(out, "continue;\n"); return;
        case CPP_RETURN: {
            StrBuf pre; sb_init(&pre);
            if (!n->return_.expr) { emit_indent(out, indent); sb_append(out, "return;\n"); return; }
            LType rt = infer_type(env, n->return_.expr);
            if (is_class_valued(rt)) {
                /* Caller-supplied "__out" convention (see cpp_lower.h) — the
                 * enclosing function was itself rewritten to take it. */
                char *a = lower_addr(env, n->return_.expr, &pre);
                emit_indent(out, indent); sb_append(out, pre.buf);
                emit_indent(out, indent); sb_appendf(out, "*(%s*)__out = *(%s*)(%s); return;\n", rt.class_name, rt.class_name, a);
                free(a); free(pre.buf);
                return;
            }
            char *e = lower_expr(env, n->return_.expr, &pre);
            emit_indent(out, indent); sb_append(out, pre.buf);
            emit_indent(out, indent); sb_appendf(out, "return %s;\n", e);
            free(e); free(pre.buf);
            return;
        }
        case CPP_VAR_DECL: {
            LType t = ltype_of_cpptype(n->var_decl.type, n->line);
            char *ctype = ctype_full(t);
            env_add(env, n->var_decl.name, t, n->var_decl.type->is_ref);
            if (n->var_decl.type->is_ref) {
                StrBuf pre; sb_init(&pre);
                char *a = n->var_decl.init ? lower_addr(env, n->var_decl.init, &pre) : xstrdup("0");
                emit_indent(out, indent); sb_append(out, pre.buf);
                emit_indent(out, indent); sb_appendf(out, "%s *%s = %s;\n", ctype, n->var_decl.name, a);
                free(a); free(pre.buf); free(ctype);
                return;
            }
            if (t.kind == LT_STRING) {
                emit_indent(out, indent); sb_appendf(out, "%s %s;\n", ctype, n->var_decl.name);
                if (n->var_decl.init) {
                    StrBuf pre; sb_init(&pre);
                    if (n->var_decl.init->kind == CPP_CALL && n->var_decl.init->call.callee == NULL) {
                        char *a = n->var_decl.init->call.argc == 1 ? lower_expr(env, n->var_decl.init->call.args[0], &pre) : xstrdup("\"\"");
                        emit_indent(out, indent); sb_append(out, pre.buf);
                        emit_indent(out, indent); sb_appendf(out, "cpp_string_ctor_cstr(&%s, %s);\n", n->var_decl.name, a);
                        free(a);
                    } else {
                        LType it = infer_type(env, n->var_decl.init);
                        if (it.kind == LT_CSTR) { char *v = lower_expr(env, n->var_decl.init, &pre); emit_indent(out, indent); sb_append(out, pre.buf); emit_indent(out, indent); sb_appendf(out, "cpp_string_ctor_cstr(&%s, %s);\n", n->var_decl.name, v); free(v); }
                        else { char *a = lower_addr(env, n->var_decl.init, &pre); emit_indent(out, indent); sb_append(out, pre.buf); emit_indent(out, indent); sb_appendf(out, "cpp_string_ctor_copy(&%s, %s);\n", n->var_decl.name, a); free(a); }
                    }
                    free(pre.buf);
                } else {
                    emit_indent(out, indent); sb_appendf(out, "cpp_string_ctor(&%s);\n", n->var_decl.name);
                }
                free(ctype);
                return;
            }
            if (t.kind == LT_VECTOR) {
                emit_indent(out, indent); sb_appendf(out, "%s %s;\n", ctype, n->var_decl.name);
                emit_indent(out, indent); sb_appendf(out, "%s__ctor(&%s);\n", t.class_name, n->var_decl.name);
                free(ctype);
                return;
            }
            if (is_class_valued(t)) {
                ClassInfo *ci = find_class(t.class_name);
                emit_indent(out, indent); sb_appendf(out, "%s %s;\n", ctype, n->var_decl.name);
                if (ci) {
                    StrBuf pre; sb_init(&pre);
                    char addrbuf[300]; snprintf(addrbuf, sizeof addrbuf, "&%s", n->var_decl.name);
                    if (n->var_decl.init && n->var_decl.init->kind == CPP_CALL && n->var_decl.init->call.callee == NULL) {
                        char *cc = ctor_call_for(ci, addrbuf, n->var_decl.init->call.args, n->var_decl.init->call.argc, env, &pre, n->line);
                        emit_indent(out, indent); sb_append(out, pre.buf);
                        if (cc) { emit_indent(out, indent); sb_appendf(out, "%s;\n", cc); free(cc); }
                    } else if (n->var_decl.init) {
                        LType it = infer_type(env, n->var_decl.init);
                        if (ltype_eq(it, t)) {
                            char *a = lower_expr(env, n->var_decl.init, &pre);
                            emit_indent(out, indent); sb_append(out, pre.buf);
                            emit_indent(out, indent); sb_appendf(out, "%s = %s;\n", n->var_decl.name, a);
                            free(a);
                        } else {
                            char *cc = ctor_call_for(ci, addrbuf, &n->var_decl.init, 1, env, &pre, n->line);
                            emit_indent(out, indent); sb_append(out, pre.buf);
                            if (cc) { emit_indent(out, indent); sb_appendf(out, "%s;\n", cc); free(cc); }
                        }
                    } else if (ci->n_ctors > 0) {
                        char *cc = ctor_call_for(ci, addrbuf, NULL, 0, env, &pre, n->line);
                        emit_indent(out, indent); sb_append(out, pre.buf);
                        if (cc) { emit_indent(out, indent); sb_appendf(out, "%s;\n", cc); free(cc); }
                    } else {
                        emit_implicit_field_init(ci, addrbuf, out, indent);
                    }
                    free(pre.buf);
                }
                free(ctype);
                return;
            }
            /* primitive */
            if (n->var_decl.init) {
                StrBuf pre; sb_init(&pre);
                char *v = lower_expr(env, n->var_decl.init, &pre);
                emit_indent(out, indent); sb_append(out, pre.buf);
                emit_indent(out, indent); sb_appendf(out, "%s %s = %s;\n", ctype, n->var_decl.name, v);
                free(v); free(pre.buf);
            } else {
                emit_indent(out, indent); sb_appendf(out, "%s %s;\n", ctype, n->var_decl.name);
            }
            free(ctype);
            return;
        }
        case CPP_EXPR_STMT: {
            CppNode *e = n->expr_stmt.expr;
            CppNode *chain[64]; int cnt = 0;
            if (e->kind == CPP_BINARY && !strcmp(e->binary.op, "<<") && cout_chain_collect(e, chain, &cnt, 64)) {
                for (int i = 0; i < cnt; i++) lower_cout_operand(env, chain[i], out, indent);
                return;
            }
            if (e->kind == CPP_BINARY && !strcmp(e->binary.op, ">>") && cin_chain_collect(e, chain, &cnt, 64)) {
                for (int i = 0; i < cnt; i++) lower_cin_operand(env, chain[i], out, indent);
                return;
            }
            if (e->kind == CPP_DELETE || e->kind == CPP_DELETE_ARRAY) {
                CppNode *target = e->kind == CPP_DELETE ? e->delete_.expr : e->delete_array.expr;
                LType t = infer_type(env, target);
                StrBuf pre; sb_init(&pre);
                char *v = lower_expr(env, target, &pre);
                emit_indent(out, indent); sb_append(out, pre.buf);
                if (t.kind == LT_CLASS && t.ptr_depth == 1) {
                    ClassInfo *ci = find_class(t.class_name);
                    if (ci && ci->has_dtor) { emit_indent(out, indent); sb_appendf(out, "%s(%s);\n", ci->dtor_mangled, v); }
                }
                emit_indent(out, indent); sb_appendf(out, "free(%s);\n", v);
                free(v); free(pre.buf);
                return;
            }
            StrBuf pre; sb_init(&pre);
            char *v = lower_expr(env, e, &pre);
            emit_indent(out, indent); sb_append(out, pre.buf);
            emit_indent(out, indent); sb_appendf(out, "%s;\n", v);
            free(v); free(pre.buf);
            return;
        }
        default:
            lower_errorf(n->line, "internal: no statement lowering for node kind %d", n->kind);
    }
}

/* ============================== class emission ============================= */
static char *coarse_cat_by_count(int argc) { char buf[16]; snprintf(buf, sizeof buf, "%d", argc); return xstrdup(buf); }

/* Broad overload-matching category — collapses int/char/bool together
 * (C's usual implicit conversions among them) but keeps double, string,
 * vector and each distinct class name apart. Used both to build a
 * disambiguating mangled-name suffix for two overloads sharing the same
 * name AND the same parameter COUNT (e.g. "int add(int,int)" vs "double
 * add(double,double)" — arg count alone collided them onto one symbol
 * name in an earlier version of this pass, a real, confirmed bug: calling
 * the double overload silently called the int one with reinterpreted
 * bits) and, via ltype_matches below, to pick the right candidate again
 * at each call site. */
static int ltype_broad_cat(LType t) {
    switch (t.kind) {
        case LT_INT: case LT_CHAR: case LT_BOOL: return 0;
        case LT_DOUBLE: return 1;
        case LT_STRING: return 2;
        case LT_CSTR: return 3;
        case LT_VECTOR: return 4;
        case LT_CLASS: return 5;
        default: return 0;
    }
}
static int ltype_matches(LType want, LType got) {
    if (want.ptr_depth != got.ptr_depth && (want.ptr_depth > 0) != (got.ptr_depth > 0)) return 0;
    int wc = ltype_broad_cat(want), gc = ltype_broad_cat(got);
    if (wc != gc) return 0;
    if (wc == 5) return want.class_name && got.class_name && !strcmp(want.class_name, got.class_name);
    return 1;
}
static char *sig_suffix_from_params(ParamSig *params, int n) {
    StrBuf b; sb_init(&b);
    for (int i = 0; i < n; i++) {
        int c = ltype_broad_cat(params[i].type);
        if (c == 5) sb_appendf(&b, "_c%s", params[i].type.class_name);
        else sb_appendf(&b, "_%d", c);
    }
    if (n == 0) sb_append(&b, "_v");
    return b.buf;
}

static void collect_class(CppNode *cdecl, const char *ns_prefix);
static void collect_func(CppNode *fdecl, const char *ns_prefix);
static void collect_decls(CppNode **decls, int n, const char *ns_prefix);

static void collect_decls(CppNode **decls, int n, const char *ns_prefix) {
    for (int i = 0; i < n; i++) {
        CppNode *d = decls[i];
        if (d->kind == CPP_NAMESPACE) {
            char *sub = catnew3(ns_prefix, d->namespace_.name, "__");
            collect_decls(d->namespace_.decls, d->namespace_.n_decls, sub);
            free(sub);
        } else if (d->kind == CPP_CLASS_DECL) collect_class(d, ns_prefix);
        else if (d->kind == CPP_FUNC_DECL) collect_func(d, ns_prefix);
    }
}

static void collect_class(CppNode *cdecl, const char *ns_prefix) {
    ClassInfo *ci = class_new();
    ci->src_name = xstrdup(cdecl->class_decl.name);
    ci->name = catnew(ns_prefix, cdecl->class_decl.name);
    ci->is_struct = cdecl->class_decl.is_struct;
    if (cdecl->class_decl.n_template_params > 0) {
        ci->is_template = 1;
        ci->n_tparams = cdecl->class_decl.n_template_params;
        ci->tparams = malloc(sizeof(char*) * ci->n_tparams);
        for (int i = 0; i < ci->n_tparams; i++) ci->tparams[i] = xstrdup(cdecl->class_decl.template_params[i]->template_param.name);
        ci->raw_decl = cdecl;
        return;
    }
    if (cdecl->class_decl.base_class_name) {
        ci->base_name = catnew(ns_prefix, cdecl->class_decl.base_class_name);
        ci->base = find_class(ci->base_name);
        if (!ci->base) { lower_errorf(cdecl->line, "base class '%s' must be declared before '%s'", cdecl->class_decl.base_class_name, ci->src_name); }
    }
    /* count overload buckets by src name among methods, and among ctors overall */
    int n_members = cdecl->class_decl.n_members;
    for (int pass = 0; pass < 1; pass++) {
        for (int i = 0; i < n_members; i++) {
            CppNode *m = cdecl->class_decl.members[i];
            if (m->kind == CPP_FIELD_DECL) {
                ci->fields = realloc(ci->fields, sizeof(FieldInfo) * (ci->n_fields + 1));
                FieldInfo *f = &ci->fields[ci->n_fields++];
                f->src_name = xstrdup(m->field_decl.name);
                f->type = ltype_of_cpptype(m->field_decl.type, m->line);
                f->is_static = m->field_decl.is_static;
            } else if (m->kind == CPP_METHOD_DECL) {
                int cnt = 0;
                for (int j = 0; j < n_members; j++) {
                    CppNode *m2 = cdecl->class_decl.members[j];
                    if (m2->kind == CPP_METHOD_DECL && !strcmp(m2->method_decl.name, m->method_decl.name) &&
                        ((m2->method_decl.op_name == NULL) == (m->method_decl.op_name == NULL)) &&
                        (m2->method_decl.op_name == NULL || !strcmp(m2->method_decl.op_name, m->method_decl.op_name)))
                        cnt++;
                }
                ci->methods = realloc(ci->methods, sizeof(MethodInfo) * (ci->n_methods + 1));
                MethodInfo *mi = &ci->methods[ci->n_methods++];
                mi->src_name = xstrdup(m->method_decl.op_name ? "operator" : m->method_decl.name);
                mi->op_name = xstrdup(m->method_decl.op_name);
                mi->ret = ltype_of_cpptype(m->method_decl.ret_type, m->line);
                mi->n_params = m->method_decl.n_params;
                mi->params = malloc(sizeof(ParamSig) * (mi->n_params ? mi->n_params : 1));
                for (int k = 0; k < mi->n_params; k++) {
                    CppNode *p = m->method_decl.params[k];
                    mi->params[k].type = ltype_of_cpptype(p->param.type, p->line);
                    mi->params[k].is_ref = p->param.type->is_ref;
                    mi->params[k].default_value = p->param.default_value;
                }
                char *base_mangle;
                if (m->method_decl.op_name) {
                    char safe[32]; int k = 0;
                    for (const char *p = m->method_decl.op_name; *p && k < 30; p++) safe[k++] = (*p=='='?'e':*p=='+'?'a':*p=='-'?'s':*p=='*'?'m':*p=='/'?'d':*p=='<'?'l':*p=='>'?'g':*p=='!'?'n':'x');
                    safe[k] = '\0';
                    base_mangle = catnew3(ci->name, "__op_", safe);
                } else base_mangle = catnew3(ci->name, "__", m->method_decl.name);
                if (cnt > 1) {
                    char *sig = sig_suffix_from_params(mi->params, mi->n_params);
                    char *full = catnew3(base_mangle, "__a", sig);
                    mi->mangled = full; free(sig); free(base_mangle);
                } else mi->mangled = base_mangle;
                mi->is_static = m->method_decl.is_static;
                mi->is_virtual = m->method_decl.is_virtual;
                mi->is_const = m->method_decl.is_const;
                if (mi->is_virtual) ci->declares_virtual = 1;
            } else if (m->kind == CPP_CTOR_DECL) {
                ci->ctors = realloc(ci->ctors, sizeof(CtorInfo) * (ci->n_ctors + 1));
                CtorInfo *co = &ci->ctors[ci->n_ctors++];
                int cnt = 0;
                for (int j = 0; j < n_members; j++) if (cdecl->class_decl.members[j]->kind == CPP_CTOR_DECL) cnt++;
                co->n_params = m->ctor_decl.n_params;
                co->params = malloc(sizeof(ParamSig) * (co->n_params ? co->n_params : 1));
                for (int k = 0; k < co->n_params; k++) {
                    CppNode *p = m->ctor_decl.params[k];
                    co->params[k].type = ltype_of_cpptype(p->param.type, p->line);
                    co->params[k].is_ref = p->param.type->is_ref;
                    co->params[k].default_value = p->param.default_value;
                }
                char *base_mangle = catnew3(ci->name, "__ctor", "");
                if (cnt > 1) {
                    char *sig = sig_suffix_from_params(co->params, co->n_params);
                    char *full = catnew3(base_mangle, "__a", sig);
                    co->mangled = full; free(sig); free(base_mangle);
                } else co->mangled = base_mangle;
            } else if (m->kind == CPP_DTOR_DECL) {
                ci->has_dtor = 1;
                ci->dtor_mangled = catnew3(ci->name, "__dtor", "");
                ci->dtor_is_virtual = m->dtor_decl.is_virtual;
                if (ci->dtor_is_virtual) ci->declares_virtual = 1;
            }
        }
    }
    ci->has_vtable = ci->declares_virtual || (ci->base && ci->base->has_vtable);
    /* A class that OVERRIDES an inherited virtual also sets
     * declares_virtual (see the CPP_METHOD_DECL branch above, which
     * doesn't distinguish "introduces a new virtual" from "overrides an
     * existing one") — but it must NOT become its own vtable root just
     * because of that, or it gets a second, redundant "__vptr" field on
     * top of the one it already inherits via "__base" embedding (a real,
     * confirmed bug: Square's own struct ended up with both its own
     * __vptr AND an embedded Shape carrying Shape's __vptr, so writes
     * through the base's vtable pointer never reached Square's slot and
     * every virtual call through a Shape* silently dispatched to Shape's
     * own base implementation). The base's own has_vtable is the only
     * thing that matters here. */
    ci->vtable_root = (ci->base && ci->base->has_vtable) ? ci->base->vtable_root : ci;
}

static void collect_func(CppNode *fdecl, const char *ns_prefix) {
    if (fdecl->func_decl.n_template_params > 0) {
        FuncInfo *fi = func_new();
        fi->src_name = xstrdup(fdecl->func_decl.name);
        fi->is_template = 1;
        fi->n_tparams = fdecl->func_decl.n_template_params;
        fi->tparams = malloc(sizeof(char*) * fi->n_tparams);
        for (int i = 0; i < fi->n_tparams; i++) fi->tparams[i] = xstrdup(fdecl->func_decl.template_params[i]->template_param.name);
        fi->raw_decl = fdecl;
        return;
    }
    FuncInfo *fi = func_new();
    fi->src_name = xstrdup(fdecl->func_decl.name);
    fi->ret = ltype_of_cpptype(fdecl->func_decl.ret_type, fdecl->line);
    fi->n_params = fdecl->func_decl.n_params;
    fi->params = malloc(sizeof(ParamSig) * (fi->n_params ? fi->n_params : 1));
    for (int i = 0; i < fi->n_params; i++) {
        CppNode *p = fdecl->func_decl.params[i];
        fi->params[i].type = ltype_of_cpptype(p->param.type, p->line);
        fi->params[i].is_ref = p->param.type->is_ref;
        fi->params[i].default_value = p->param.default_value;
    }
    fi->mangled = catnew(ns_prefix, fdecl->func_decl.name);
    fi->raw_decl = fdecl;
}

/* Disambiguates free-function overloads — collect_func above always names
 * a fresh FuncInfo "ns_prefix+name" first; called once after every
 * top-level decl has been collected (so every overload of a given name is
 * already registered) to rename each colliding group with a
 * sig_suffix_from_params suffix, the same fix collect_class's own
 * method/ctor mangling needed (see sig_suffix_from_params' own comment:
 * two overloads sharing only a bare name previously became the exact same
 * C symbol, and calling the second one silently ran the first with
 * reinterpreted argument bits). */
static void finalize_func_overloads(void) {
    for (int i = 0; i < g_n_funcs; i++) {
        if (g_funcs[i]->is_template || !g_funcs[i]->mangled) continue;
        int cnt = 0;
        for (int j = 0; j < g_n_funcs; j++)
            if (!g_funcs[j]->is_template && g_funcs[j]->mangled && !strcmp(g_funcs[j]->mangled, g_funcs[i]->mangled)) cnt++;
        if (cnt <= 1) continue;
        char *sig = sig_suffix_from_params(g_funcs[i]->params, g_funcs[i]->n_params);
        char *full = catnew3(g_funcs[i]->mangled, "__a", sig);
        free(g_funcs[i]->mangled);
        g_funcs[i]->mangled = full;
        free(sig);
    }
}

/* --------------------------- emit struct/vtable ---------------------------- */
static void emit_class_struct(ClassInfo *ci) {
    if (ci->emitted) return;
    if (ci->base) emit_class_struct(ci->base);
    ci->emitted = 1;
    sb_appendf(&g_out_types, "typedef struct %s %s;\n", ci->name, ci->name);
    if (ci->has_vtable) {
        ClassInfo *root = ci->vtable_root;
        if (ci == root) {
            sb_appendf(&g_out_types, "typedef struct %sVTable {\n", root->name);
            for (int i = 0; i < root->n_methods; i++) {
                MethodInfo *m = &root->methods[i];
                if (!m->is_virtual) continue;
                char *rc = ctype_full(m->ret);
                sb_appendf(&g_out_types, "    %s (*%s)(void *__this", rc, m->src_name);
                for (int k = 0; k < m->n_params; k++) { char *pc = ctype_full(m->params[k].type); sb_appendf(&g_out_types, ", %s", pc); free(pc); }
                sb_append(&g_out_types, ");\n");
                free(rc);
            }
            sb_appendf(&g_out_types, "} %sVTable;\n", root->name);
        }
    }
    sb_appendf(&g_out_types, "struct %s {\n", ci->name);
    if (ci->has_vtable && ci == ci->vtable_root) sb_appendf(&g_out_types, "    %sVTable *__vptr;\n", ci->vtable_root->name);
    if (ci->base) sb_appendf(&g_out_types, "    %s __base;\n", ci->base->name);
    for (int i = 0; i < ci->n_fields; i++) {
        if (ci->fields[i].is_static) continue;
        char *ct = ctype_full(ci->fields[i].type);
        sb_appendf(&g_out_types, "    %s %s;\n", ct, ci->fields[i].src_name);
        free(ct);
    }
    sb_append(&g_out_types, "};\n");
    for (int i = 0; i < ci->n_fields; i++) {
        if (!ci->fields[i].is_static) continue;
        char *ct = ctype_full(ci->fields[i].type);
        sb_appendf(&g_out_types, "%s %s_%s;\n", ct, ci->name, ci->fields[i].src_name);
        free(ct);
    }
}

static MethodInfo *find_effective(ClassInfo *ci, const char *src_name, int n_params_root, ClassInfo **owner) {
    /* Walk from `ci` UP toward the root, returning the nearest override of
     * root's slot (matched by src_name only — see cpp_lower.h's own scope
     * note: a derived class may only override, not add, virtual methods). */
    for (ClassInfo *c = ci; c; c = c->base) {
        for (int i = 0; i < c->n_methods; i++)
            if (c->methods[i].is_virtual && !strcmp(c->methods[i].src_name, src_name)) { if (owner) *owner = c; return &c->methods[i]; }
    }
    (void)n_params_root;
    return NULL;
}

static void emit_vtable_instance(ClassInfo *ci) {
    if (!ci->has_vtable) return;
    ClassInfo *root = ci->vtable_root;
    sb_appendf(&g_out_funcs, "static %sVTable %s_vtable_instance = {\n", root->name, ci->name);
    int first = 1;
    for (int i = 0; i < root->n_methods; i++) {
        MethodInfo *rm = &root->methods[i];
        if (!rm->is_virtual) continue;
        ClassInfo *owner = root;
        MethodInfo *eff = find_effective(ci, rm->src_name, rm->n_params, &owner);
        if (!eff) { eff = rm; owner = root; }
        if (!first) sb_append(&g_out_funcs, ",\n");
        first = 0;
        sb_appendf(&g_out_funcs, "    %s", eff->mangled);
    }
    sb_append(&g_out_funcs, "\n};\n");
}

static char *vptr_path_from_instance(ClassInfo *ci) {
    /* Dot-path from an instance of `ci` down to the (unique) vptr field,
     * which lives inside the vtable-root's own struct — e.g. "__base." for
     * one level of embedding, "" if ci itself is the root. */
    StrBuf b; sb_init(&b);
    for (ClassInfo *c = ci; c && c != ci->vtable_root; c = c->base) sb_append(&b, "__base.");
    return b.buf;
}

/* ---------------------------- emit method bodies --------------------------- */
static char *params_c_text(MethodInfo *m) {
    StrBuf b; sb_init(&b);
    sb_append(&b, "void *__this");
    for (int i = 0; i < m->n_params; i++) {
        char *ct = ctype_full(m->params[i].type);
        sb_appendf(&b, ", %s %s%s", ct, m->params[i].is_ref ? "*" : "", "p");
        free(ct);
    }
    return b.buf;
}

/* Every emit_* function below builds its COMPLETE text (signature through
 * closing brace) into a private local StrBuf and appends that to
 * g_out_funcs only once, at the very end — never the signature first and
 * the lowered body afterward as two separate appends. Lowering a body can
 * itself have side effects that append to g_out_funcs (a template/vector
 * instantiation triggered by a local variable's type, e.g. "Stack<int>
 * st;" inside main — see ensure_class_instantiated_members/
 * ensure_vector_instantiated), and those need to land BEFORE this
 * function's own text in the output, not interleaved into the middle of
 * it. A real, confirmed bug: the two-step version emitted "int main(void)
 * {" into g_out_funcs immediately, then lowered main's body (which
 * pulled in cpp_vector__int's function definitions mid-stream), then
 * appended main's own statements — producing invalid C with an entire
 * struct's worth of function definitions sitting between main's opening
 * brace and its first statement. */
static void emit_method(ClassInfo *ci, CppNode *mdecl, int method_idx) {
    MethodInfo *m = (method_idx >= 0 && method_idx < ci->n_methods) ? &ci->methods[method_idx] : NULL;
    if (!m) return;
    if (!mdecl->method_decl.body) return;
    int ret_is_class = is_class_valued(m->ret);
    char *rct = ret_is_class ? xstrdup("void") : ctype_full(m->ret);

    Env env; env_init(&env); env_push(&env);
    ClassInfo *save_cls = g_cur_class; g_cur_class = ci;
    for (int i = 0; i < m->n_params; i++) {
        char pname[16]; snprintf(pname, sizeof pname, "p%d", i);
        env_add_named(&env, mdecl->method_decl.params[i]->param.name, pname, m->params[i].type, m->params[i].is_ref);
    }
    StrBuf body; sb_init(&body);
    for (int i = 0; i < mdecl->method_decl.body->block.n_stmts; i++) lower_stmt(&env, mdecl->method_decl.body->block.stmts[i], &body, 1);
    g_cur_class = save_cls;
    env_pop(&env);

    StrBuf full; sb_init(&full);
    sb_appendf(&full, "%s %s(void *__this%s", rct, m->mangled, ret_is_class ? ", void *__out" : "");
    for (int i = 0; i < m->n_params; i++) {
        char *ct = ctype_full(m->params[i].type);
        sb_appendf(&full, ", %s %sp%d", ct, m->params[i].is_ref ? "*" : "", i);
        free(ct);
    }
    sb_append(&full, ") {\n");
    sb_appendf(&full, "    %s *__this_typed = (%s*)__this;\n", ci->name, ci->name);
    sb_append(&full, body.buf);
    sb_append(&full, "}\n");
    sb_append(&g_out_funcs, full.buf);
    free(full.buf); free(body.buf); free(rct);
}

static void emit_ctor(ClassInfo *ci, CppNode *cdecl, int ctor_idx) {
    CtorInfo *c = (ctor_idx >= 0 && ctor_idx < ci->n_ctors) ? &ci->ctors[ctor_idx] : NULL;
    if (!c) return;

    Env env; env_init(&env); env_push(&env);
    ClassInfo *save_cls = g_cur_class; g_cur_class = ci;
    for (int i = 0; i < c->n_params; i++) { char pname[16]; snprintf(pname, sizeof pname, "p%d", i); env_add_named(&env, cdecl->ctor_decl.params[i]->param.name, pname, c->params[i].type, c->params[i].is_ref); }

    StrBuf body; sb_init(&body);
    if (ci->has_vtable && ci == ci->vtable_root) sb_appendf(&body, "    __this_typed->__vptr = &%s_vtable_instance;\n", ci->name);
    else if (ci->has_vtable) { char *path = vptr_path_from_instance(ci); sb_appendf(&body, "    __this_typed->%s__vptr = &%s_vtable_instance;\n", path, ci->name); free(path); }
    if (ci->base && cdecl->ctor_decl.init_list_target) {
        char *cc = ctor_call_for(ci->base, "&(__this_typed->__base)", cdecl->ctor_decl.init_list_args, cdecl->ctor_decl.n_init_args, &env, &body, cdecl->line);
        if (cc) { sb_appendf(&body, "    %s;\n", cc); free(cc); }
    } else if (ci->base && ci->base->n_ctors > 0) {
        char *cc = ctor_call_for(ci->base, "&(__this_typed->__base)", NULL, 0, &env, &body, cdecl->line);
        if (cc) { sb_appendf(&body, "    %s;\n", cc); free(cc); }
    }
    if (cdecl->ctor_decl.body)
        for (int i = 0; i < cdecl->ctor_decl.body->block.n_stmts; i++) lower_stmt(&env, cdecl->ctor_decl.body->block.stmts[i], &body, 1);
    g_cur_class = save_cls;
    env_pop(&env);

    StrBuf full; sb_init(&full);
    sb_appendf(&full, "void %s(void *__this", c->mangled);
    for (int i = 0; i < c->n_params; i++) { char *ct = ctype_full(c->params[i].type); sb_appendf(&full, ", %s %sp%d", ct, c->params[i].is_ref ? "*" : "", i); free(ct); }
    sb_append(&full, ") {\n");
    sb_appendf(&full, "    %s *__this_typed = (%s*)__this;\n", ci->name, ci->name);
    sb_append(&full, body.buf);
    sb_append(&full, "}\n");
    sb_append(&g_out_funcs, full.buf);
    free(full.buf); free(body.buf);
}

static void emit_dtor(ClassInfo *ci, CppNode *ddecl) {
    Env env; env_init(&env); env_push(&env);
    ClassInfo *save_cls = g_cur_class; g_cur_class = ci;
    StrBuf body; sb_init(&body);
    if (ddecl->dtor_decl.body)
        for (int i = 0; i < ddecl->dtor_decl.body->block.n_stmts; i++) lower_stmt(&env, ddecl->dtor_decl.body->block.stmts[i], &body, 1);
    if (ci->base && ci->base->has_dtor) sb_appendf(&body, "    %s(&(__this_typed->__base));\n", ci->base->dtor_mangled);
    g_cur_class = save_cls;
    env_pop(&env);

    StrBuf full; sb_init(&full);
    sb_appendf(&full, "void %s(void *__this) {\n", ci->dtor_mangled);
    sb_appendf(&full, "    %s *__this_typed = (%s*)__this;\n", ci->name, ci->name);
    sb_append(&full, body.buf);
    sb_append(&full, "}\n");
    sb_append(&g_out_funcs, full.buf);
    free(full.buf); free(body.buf);
}

static void emit_class_body(ClassInfo *ci, CppNode *cdecl) {
    emit_class_struct(ci);
    if (ci->has_vtable) emit_vtable_instance(ci);
    /* Running indices into ci->methods/ci->ctors, populated by collect_class
     * in this SAME member-iteration order — used instead of re-matching by
     * name/param-count here, which broke the moment two ctors (or two
     * methods) shared the same parameter count (see sig_suffix_from_params'
     * own comment on that class of overload-mangling bug: matching by count
     * alone here would emit BOTH source bodies into the FIRST candidate's
     * slot, silently dropping the second overload's real body). */
    int method_idx = 0, ctor_idx = 0;
    for (int i = 0; i < cdecl->class_decl.n_members; i++) {
        CppNode *m = cdecl->class_decl.members[i];
        if (m->kind == CPP_METHOD_DECL) { emit_method(ci, m, method_idx); method_idx++; }
        else if (m->kind == CPP_CTOR_DECL) { emit_ctor(ci, m, ctor_idx); ctor_idx++; }
        else if (m->kind == CPP_DTOR_DECL) emit_dtor(ci, m);
    }
}

static void emit_func_body(FuncInfo *fi, CppNode *fdecl) {
    if (!fdecl->func_decl.body) return;
    int ret_is_class = is_class_valued(fi->ret);
    char *rct = ret_is_class ? xstrdup("void") : ctype_full(fi->ret);

    Env env; env_init(&env); env_push(&env);
    ClassInfo *save_cls = g_cur_class; g_cur_class = NULL;
    for (int i = 0; i < fi->n_params; i++) { char pname[16]; snprintf(pname, sizeof pname, "p%d", i); env_add_named(&env, fdecl->func_decl.params[i]->param.name, pname, fi->params[i].type, fi->params[i].is_ref); }
    StrBuf body; sb_init(&body);
    for (int i = 0; i < fdecl->func_decl.body->block.n_stmts; i++) lower_stmt(&env, fdecl->func_decl.body->block.stmts[i], &body, 1);
    g_cur_class = save_cls;
    env_pop(&env);

    StrBuf full; sb_init(&full);
    sb_appendf(&full, "%s %s(", rct, fi->mangled);
    if (ret_is_class) sb_append(&full, "void *__out");
    for (int i = 0; i < fi->n_params; i++) {
        char *ct = ctype_full(fi->params[i].type);
        sb_appendf(&full, "%s%s %sp%d", (ret_is_class || i > 0) ? ", " : "", ct, fi->params[i].is_ref ? "*" : "", i);
        free(ct);
    }
    if (fi->n_params == 0 && !ret_is_class) sb_append(&full, "void");
    sb_append(&full, ") {\n");
    sb_append(&full, body.buf);
    sb_append(&full, "}\n");
    sb_append(&g_out_funcs, full.buf);
    free(full.buf); free(body.buf); free(rct);
}

/* Renames every use of `p0`'s name within a cloned param list — used by
 * emit params for correctness; real param names come straight from the
 * source AST (mdecl->...->param.name), so this note only documents that
 * lower_stmt/lower_expr always resolve identifiers via env, never via
 * "p<N>" positional names — the "p%d" spelling above is just the C
 * parameter's OWN name, matched 1:1 to env_add() calls right after. */

/* Fixes emit_method's earlier parameter emission to use the same "p%d"
 * spelling env_add expects (done inline above already) — kept as a no-op
 * placeholder removed; see emit_method. */

static void ensure_class_instantiated_members(ClassInfo *tmpl, CppType *arg, int line) {
    CppType default_arg_storage; CppType *use_arg = arg;
    if (!use_arg) { default_arg_storage = *cpptype_new("int"); use_arg = &default_arg_storage; }
    char *mangled = catnew3(tmpl->src_name, "__", use_arg->name);
    for (char *p = mangled; *p; p++) if (*p == ':') *p = '_';
    if (find_class(mangled)) { free(mangled); return; }
    /* Deep-clone tmpl->raw_decl substituting the single type parameter,
     * then process it exactly like an ordinary (non-template) class. The
     * member AST nodes are reused (not text-cloned) since each
     * instantiation is only ever lowered once and tmpl's own AST is never
     * needed again afterward — the per-instantiation substitution itself
     * ("T" -> a concrete type) is purely a lowering-time lookup via
     * g_tsub_name/g_tsub_type (see ltype_of_cpptype), not a tree edit. */
    CppNode *raw = tmpl->raw_decl;
    CppNode **members = malloc(sizeof(CppNode*) * (raw->class_decl.n_members ? raw->class_decl.n_members : 1));
    for (int i = 0; i < raw->class_decl.n_members; i++) members[i] = raw->class_decl.members[i];
    CppNode fake;
    memset(&fake, 0, sizeof fake);
    fake.kind = CPP_CLASS_DECL;
    fake.line = line;
    fake.class_decl.name = mangled;
    fake.class_decl.is_struct = raw->class_decl.is_struct;
    fake.class_decl.base_class_name = NULL;
    fake.class_decl.members = members;
    fake.class_decl.n_members = raw->class_decl.n_members;

    char *save_name = g_tsub_name; CppType *save_type = g_tsub_type;
    g_tsub_name = tmpl->tparams[0]; g_tsub_type = use_arg;
    collect_class(&fake, "");
    ClassInfo *ci = find_class(mangled);
    if (ci) emit_class_body(ci, &fake);
    g_tsub_name = save_name; g_tsub_type = save_type;
    free(members);
}

/* Function-template instantiation — mirrors ensure_class_instantiated_members
 * above (same single-type-parameter substitution mechanism), triggered
 * lazily by find_func_overload the first time a template function is
 * actually called, with T inferred from the first CALL ARGUMENT's static
 * type (not from an explicit "func<int>(...)" syntax, which this parser
 * doesn't accept — see cpp_parser.h's own documented grammar scope: no
 * explicit template-argument-list call syntax). */
static FuncInfo *ensure_func_template_instantiated(FuncInfo *tmpl, LType arg0, int line) {
    char *arg_ctype = ctype_full(arg0);
    char mangled[256]; snprintf(mangled, sizeof mangled, "%s__%s", tmpl->src_name, arg_ctype);
    for (char *p = mangled; *p; p++) if (*p == '*') *p = 'p'; else if (*p == ' ') *p = '_';
    for (int i = 0; i < g_n_funcs; i++) if (!g_funcs[i]->is_template && g_funcs[i]->mangled && !strcmp(g_funcs[i]->mangled, mangled)) { free(arg_ctype); return g_funcs[i]; }

    CppType subst; memset(&subst, 0, sizeof subst);
    subst.name = xstrdup(base_ctype_str(arg0)); subst.ptr_depth = arg0.ptr_depth;

    CppNode *raw = tmpl->raw_decl;
    CppNode fake = *raw;
    fake.func_decl.name = mangled;
    fake.func_decl.template_params = NULL;
    fake.func_decl.n_template_params = 0;

    char *save_name = g_tsub_name; CppType *save_type = g_tsub_type;
    g_tsub_name = tmpl->tparams[0]; g_tsub_type = &subst;
    /* collect_func() always appends exactly one entry for a non-template
     * decl — grab it directly by position rather than searching for
     * "raw_decl == &fake": &fake is this call's own STACK address, which
     * gets reused across separate (sequential, same-depth) invocations of
     * this function, so two different instantiations could end up with
     * the exact same raw_decl pointer value — a real, confirmed bug
     * (maxval<int> and maxval<double>, instantiated one after another,
     * collided this way: the search matched maxval<int>'s OWN earlier
     * entry first and re-emitted its body a second time, while
     * maxval<double>'s real entry never got emitted at all, surfacing as
     * an "undefined symbol: maxval__double" link error). */
    collect_func(&fake, "");
    FuncInfo *fi = g_funcs[g_n_funcs - 1];
    if (fi) emit_func_body(fi, &fake);
    g_tsub_name = save_name; g_tsub_type = save_type;
    free(arg_ctype);
    return fi;
}

/* ================================ driver ================================= */
static void emit_decls(CppNode **decls, int n, const char *ns_prefix);

static void emit_decls(CppNode **decls, int n, const char *ns_prefix) {
    for (int i = 0; i < n; i++) {
        CppNode *d = decls[i];
        if (d->kind == CPP_NAMESPACE) {
            char *sub = catnew3(ns_prefix, d->namespace_.name, "__");
            emit_decls(d->namespace_.decls, d->namespace_.n_decls, sub);
            free(sub);
        } else if (d->kind == CPP_CLASS_DECL) {
            if (d->class_decl.n_template_params > 0) continue;
            ClassInfo *ci = find_class(catnew(ns_prefix, d->class_decl.name));
            char *nm = catnew(ns_prefix, d->class_decl.name); ci = find_class(nm); free(nm);
            if (ci) emit_class_body(ci, d);
        } else if (d->kind == CPP_FUNC_DECL) {
            if (d->func_decl.n_template_params > 0) continue;
            /* Matched by raw_decl identity, not by re-deriving "ns_prefix+
             * name" and comparing to ->mangled — finalize_func_overloads()
             * may have since renamed ->mangled to include a disambiguating
             * signature suffix (see its own comment), which broke this
             * exact-string match for any overloaded free function: the
             * lookup silently found nothing, so ONE of two overloads never
             * got its body emitted at all (surfaced as a real "undefined
             * symbol" link error the first time two same-arg-count
             * overloads were tried). */
            FuncInfo *fi = NULL;
            for (int k = 0; k < g_n_funcs; k++) if (g_funcs[k]->raw_decl == d) { fi = g_funcs[k]; break; }
            if (fi) emit_func_body(fi, d);
        } else if (d->kind == CPP_VAR_DECL) {
            Env env; env_init(&env); env_push(&env);
            LType t = ltype_of_cpptype(d->var_decl.type, d->line);
            char *ct = ctype_full(t);
            if (d->var_decl.init && !is_class_valued(t) && t.kind != LT_STRING && t.kind != LT_VECTOR) {
                StrBuf pre; sb_init(&pre);
                char *v = lower_expr(&env, d->var_decl.init, &pre);
                sb_appendf(&g_out_types, "%s %s = %s;\n", ct, d->var_decl.name, v);
                free(v); free(pre.buf);
            } else {
                sb_appendf(&g_out_types, "%s %s;\n", ct, d->var_decl.name);
            }
            free(ct);
            env_pop(&env);
        }
    }
}

static void emit_includes(CppNode **decls, int n, StrBuf *out) {
    static const char *logical[] = { "iostream", "string", "vector", "cstdio", "cstdlib", "cmath", "cstring", NULL };
    for (int i = 0; i < n; i++) {
        CppNode *d = decls[i];
        if (d->kind == CPP_NAMESPACE) { emit_includes(d->namespace_.decls, d->namespace_.n_decls, out); continue; }
        if (d->kind != CPP_INCLUDE_RAW) continue;
        const char *t = d->include_raw.text;
        while (*t == ' ') t++;
        if (strncmp(t, "include", 7) != 0) { sb_appendf(out, "#%s\n", t); continue; }
        const char *rest = t + 7; while (*rest == ' ') rest++;
        int is_logical = 0;
        if (*rest == '<') {
            char name[64]; int k = 0; const char *p = rest + 1;
            while (*p && *p != '>' && k < 63) name[k++] = *p++;
            name[k] = '\0';
            for (int j = 0; logical[j]; j++) if (!strcmp(logical[j], name)) { is_logical = 1; break; }
            if (!strcmp(name, "cstdio")) sb_append(out, "#include <stdio.h>\n");
            if (!strcmp(name, "cstdlib")) sb_append(out, "#include <stdlib.h>\n");
            if (!strcmp(name, "cstring")) sb_append(out, "#include <string.h>\n");
            if (!strcmp(name, "cmath")) sb_append(out, "#include <math.h>\n");
        }
        if (!is_logical) sb_appendf(out, "#%s\n", t);
    }
}

CppLowerResult cpp_lower_unit(CppNode *unit, const char *rt_header_name) {
    g_error_count = 0;
    g_n_classes = 0; g_n_funcs = 0; g_n_vecs = 0; g_tmp_counter = 0;
    g_cur_class = NULL;
    sb_init(&g_out_types);
    sb_init(&g_out_funcs);

    StrBuf out; sb_init(&out);
    sb_appendf(&out, "#include \"%s\"\n", rt_header_name);
    sb_append(&out, "typedef CppString cpp_string;\n");
    emit_includes(unit->unit.decls, unit->unit.n_decls, &out);

    collect_decls(unit->unit.decls, unit->unit.n_decls, "");
    finalize_func_overloads();
    emit_decls(unit->unit.decls, unit->unit.n_decls, "");

    sb_append(&out, g_out_types.buf);
    sb_append(&out, g_out_funcs.buf);
    free(g_out_types.buf);
    free(g_out_funcs.buf);

    CppLowerResult r;
    r.error_count = g_error_count;
    r.ok = g_error_count == 0;
    r.text = r.ok ? out.buf : NULL;
    if (!r.ok) free(out.buf);
    return r;
}
