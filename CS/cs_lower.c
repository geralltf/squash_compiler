#include "cs_lower.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- growable string buffer ---- */
typedef struct { char *data; int len; int cap; } StrBuf;

static void sb_init(StrBuf *b) { b->cap = 4096; b->data = (char *)malloc((unsigned int)b->cap); b->data[0] = 0; b->len = 0; }
static void sb_ensure(StrBuf *b, int extra) {
    if (b->len + extra + 1 > b->cap) {
        while (b->len + extra + 1 > b->cap) b->cap *= 2;
        b->data = (char *)realloc(b->data, (unsigned int)b->cap);
    }
}
static void sb_append(StrBuf *b, const char *s) {
    int n = (int)strlen(s);
    sb_ensure(b, n);
    memcpy(b->data + b->len, s, (unsigned int)n + 1);
    b->len += n;
}
static void sb_appendf(StrBuf *b, const char *fmt, ...) {
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    sb_append(b, tmp);
}

/* ---- error reporting ---- */
static int g_error_count;
static void lower_error(int line, const char *fmt, ...) {
    va_list ap;
    g_error_count++;
    fprintf(stderr, "cs_lower: line %d: ", line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/* ---- program-wide class registry ---- */
typedef struct {
    CsNode **classes; int n_classes; int type_id_next; int *type_ids;
    /* Phase 6e: interfaces, registered separately from classes/structs
     * (they carry no fields/ctor/instance-storage to lower -- only
     * method SIGNATURES, used to build each implementing class's real
     * vtable). Each interface's index in this array IS its "interface
     * id" -- deterministic within one compile (collect_classes walks
     * decls in file order), which is all a single-program vtable-
     * dispatch switch (see lower_interface_vtables()) ever needs -- no
     * runtime registration API required, unlike the general per-class
     * (type_id, interface_id) registry the plan originally sketched:
     * every (class, interface) pairing implemented anywhere in this
     * program is already known at LOWERING time, so the dispatch table
     * itself can just be generated, compile-time-constant C code. */
    CsNode **interfaces; int n_interfaces;
} ClassRegistry;

static void collect_classes(CsNode *decl, ClassRegistry *reg) {
    if (!decl) return;
    if (decl->kind == CS_CLASS_DECL || decl->kind == CS_STRUCT_DECL) {
        reg->classes = (CsNode **)realloc(reg->classes, sizeof(CsNode *) * (unsigned int)(reg->n_classes + 1));
        reg->type_ids = (int *)realloc(reg->type_ids, sizeof(int) * (unsigned int)(reg->n_classes + 1));
        reg->type_ids[reg->n_classes] = reg->type_id_next++;
        reg->classes[reg->n_classes] = decl;
        reg->n_classes++;
    } else if (decl->kind == CS_INTERFACE_DECL) {
        reg->interfaces = (CsNode **)realloc(reg->interfaces, sizeof(CsNode *) * (unsigned int)(reg->n_interfaces + 1));
        reg->interfaces[reg->n_interfaces] = decl;
        reg->n_interfaces++;
    } else if (decl->kind == CS_NAMESPACE) {
        int i;
        for (i = 0; i < decl->namespace_decl.n_decls; i++) collect_classes(decl->namespace_decl.decls[i], reg);
    }
}

static CsNode *reg_find_class(ClassRegistry *reg, const char *name) {
    int i;
    for (i = 0; i < reg->n_classes; i++) if (strcmp(reg->classes[i]->class_decl.name, name) == 0) return reg->classes[i];
    return 0;
}
static int reg_type_id(ClassRegistry *reg, CsNode *class_decl) {
    int i;
    for (i = 0; i < reg->n_classes; i++) if (reg->classes[i] == class_decl) return reg->type_ids[i];
    return 0;
}
static CsNode *reg_find_interface(ClassRegistry *reg, const char *name) {
    int i;
    for (i = 0; i < reg->n_interfaces; i++) if (strcmp(reg->interfaces[i]->class_decl.name, name) == 0) return reg->interfaces[i];
    return 0;
}
/* True if `cls` (a class/struct decl) implements `iface_name` -- checks
 * both "interface_names" (class_decl.interface_names, populated by
 * cs_parser.c's ":" handling for every name AFTER the first) and
 * "base_class_name" (the FIRST name after ":"). The parser can't tell a
 * base class from an interface at parse time (no symbol table yet -- C#
 * syntax is genuinely ambiguous there, "class Circle : IShape" and
 * "class Circle : SomeBaseClass" parse identically), so it always
 * assigns the first name to base_class_name as a guess; this lowering
 * pass, which DOES have every interface registered by now, corrects that
 * guess here rather than in the parser. Real class inheritance itself is
 * still out of scope (base_class_name is otherwise unused anywhere in
 * this file) -- this only re-checks it as a possible interface name, the
 * one case that actually needs to work for Phase 6e. */
static int class_implements(CsNode *cls, const char *iface_name, ClassRegistry *reg) {
    int i;
    if (cls->class_decl.base_class_name && strcmp(cls->class_decl.base_class_name, iface_name) == 0 && reg_find_interface(reg, iface_name)) return 1;
    for (i = 0; i < cls->class_decl.n_interfaces; i++)
        if (strcmp(cls->class_decl.interface_names[i], iface_name) == 0) return 1;
    return 0;
}

static CsNode *class_find_field(CsNode *cls, const char *name) {
    int i;
    for (i = 0; i < cls->class_decl.n_members; i++) {
        CsNode *m = cls->class_decl.members[i];
        if (m->kind == CS_FIELD_DECL && strcmp(m->field_decl.name, name) == 0) return m;
        if (m->kind == CS_PROPERTY_DECL && strcmp(m->property_decl.name, name) == 0) return m;
    }
    return 0;
}
static CsNode *class_find_method(CsNode *cls, const char *name) {
    int i;
    for (i = 0; i < cls->class_decl.n_members; i++) {
        CsNode *m = cls->class_decl.members[i];
        if (m->kind == CS_METHOD_DECL && strcmp(m->method_decl.name, name) == 0) return m;
    }
    return 0;
}
static CsNode *class_find_ctor(CsNode *cls) {
    int i;
    for (i = 0; i < cls->class_decl.n_members; i++) if (cls->class_decl.members[i]->kind == CS_CTOR_DECL) return cls->class_decl.members[i];
    return 0;
}

/* ---- type lowering ---- */
static const char *primitive_c_type(const char *csname) {
    if (!strcmp(csname, "int")) return "int";
    if (!strcmp(csname, "long")) return "long long";
    if (!strcmp(csname, "short")) return "short";
    if (!strcmp(csname, "byte")) return "unsigned char";
    if (!strcmp(csname, "sbyte")) return "signed char";
    if (!strcmp(csname, "uint")) return "unsigned int";
    if (!strcmp(csname, "ulong")) return "unsigned long long";
    if (!strcmp(csname, "ushort")) return "unsigned short";
    if (!strcmp(csname, "bool")) return "int";
    if (!strcmp(csname, "double")) return "double";
    if (!strcmp(csname, "float")) return "float";
    if (!strcmp(csname, "char")) return "char";
    if (!strcmp(csname, "void")) return "void";
    if (!strcmp(csname, "object")) return "void*";
    if (!strcmp(csname, "string")) return "CsString*";
    /* Phase 6c: opaque native handles for P/Invoke declarations --
     * IntPtr/nint are C#'s own spellings for "an opaque native pointer-
     * sized value", exactly what a bodyless native function taking e.g. a
     * VkInstance dispatchable handle needs. */
    if (!strcmp(csname, "IntPtr")) return "void*";
    if (!strcmp(csname, "nint")) return "void*";
    return 0;
}

/* Returns a malloc'd C type spelling for `t`. Generic types (List<T>,
 * Dictionary<K,V>, or a user generic class) and arrays report an error
 * and return "void*" as a best-effort placeholder so the rest of the
 * lowering pass can keep going and surface every error in one run
 * (matching cs_parser.c's own "report everything" recovery convention)
 * instead of aborting at the first one. */
static char *lower_type_str(CsType *t, ClassRegistry *reg, int line) {
    const char *prim;
    if (!t) { lower_error(line, "internal: NULL type in lower_type_str"); return cs_strdup("void*"); }
    if (t->array_rank > 0) { lower_error(line, "arrays are not supported yet (type '%s') -- tracked follow-up, see cs_lower.h", t->name); return cs_strdup("void*"); }
    /* List<T> -> CsList* (generic-erased, matching CSR/csharp_rt.h's own
     * CsList design -- NOT per-instantiation monomorphized structs; T only
     * drives sizeof()/temp-variable typing at each use site, see
     * type_is_list()/mc_new_temp()). Every other generic shape (
     * Dictionary<K,V>, a user-defined generic class) is still out of
     * scope, reported below. */
    if (t->n_type_args > 0) {
        if (strcmp(t->name, "List") == 0 && t->n_type_args == 1) return cs_strdup("CsList*");
        lower_error(line, "generic types other than List<T> are not supported yet ('%s<...>') -- tracked follow-up, see cs_lower.h", t->name);
        return cs_strdup("void*");
    }
    prim = primitive_c_type(t->name);
    if (prim) return cs_strdup(prim);
    {
        CsNode *cls = reg_find_class(reg, t->name);
        if (cls) {
            /* Real C# value-vs-reference-type distinction, Phase 6b: a
             * `struct` (CS_STRUCT_DECL) is a plain C struct VALUE -- no
             * pointer, no GC header, matching real C# semantics AND
             * enabling native interop (a native function expecting a
             * flat VkExtent2D-shaped value can't be handed a GC pointer).
             * `class` (CS_CLASS_DECL) keeps the existing GC-heap-pointer
             * behavior. Before this phase both kinds were lowered
             * identically (always a pointer) -- see cs_lower.h's Phase 6
             * plan notes. */
            if (cls->kind == CS_STRUCT_DECL) return cs_strdup(t->name);
            {
                char buf[256];
                snprintf(buf, sizeof buf, "%s*", t->name);
                return cs_strdup(buf);
            }
        }
    }
    /* Phase 6e: an interface-typed local/field/parameter lowers to a
     * plain, type-erased "void*" -- it can hold a pointer to any class
     * implementing the interface (there's no single common C struct
     * layout across unrelated classes the way a real base-class pointer
     * would have one), so the underlying object's real layout is only
     * ever accessed indirectly, through a per-interface vtable resolved
     * at the call site by the object's own runtime type id -- see
     * lower_interface_vtables()/the CS_MEMBER-call dispatch this enables
     * for the actual mechanism. */
    if (reg_find_interface(reg, t->name)) return cs_strdup("void*");
    lower_error(line, "unknown type '%s'", t->name);
    return cs_strdup("void*");
}

/* ---- per-method local-variable static-type tracking (see cs_lower.h) ---- */
typedef struct { char *name; CsType *type; } LocalVarInfo;
/* One compiler-generated temporary, needed wherever C# code needs a real
 * addressable lvalue that plain C expression syntax can't materialize
 * inline (squash's C parser accepts neither C99 compound literals nor
 * GNU statement-expressions -- both confirmed unsupported this session)
 * -- e.g. `list.Add(5)` lowers to `(__tmp0 = 5, csr_list_add(list,
 * &__tmp0))` via the C comma operator, where `__tmp0` must be a real,
 * already-declared local. Declared once per generated function, hoisted
 * to the very top of its body (see lower_class_methods()) -- C doesn't
 * care that a local is declared before it's textually used later in the
 * same block, only that it's declared somewhere in an enclosing scope
 * before the first REFERENCE, which "top of function" always satisfies. */
typedef struct { char *name; char *ctype; } ExtraTemp;
typedef struct {
    LocalVarInfo *locals; int n_locals; int cap_locals;
    CsNode *class_decl;     /* enclosing class, or NULL for a free function (not used yet) */
    int is_instance;        /* 1 = has a real `this` */
    ClassRegistry *reg;
    ExtraTemp *extra_temps; int n_extra_temps; int cap_extra_temps;
    int temp_counter;
} MethodCtx;

/* Registers a new hoisted temp of C type `ctype` (e.g. "int", "CsString*")
 * and returns its freshly generated name (owned by the caller -- copy
 * into generated text, then free it; the ExtraTemp registration itself
 * keeps its own copy for the declaration emitted later). */
static char *mc_new_temp(MethodCtx *mc, const char *ctype) {
    char buf[32];
    snprintf(buf, sizeof buf, "__tmp%d", mc->temp_counter++);
    if (mc->n_extra_temps >= mc->cap_extra_temps) {
        mc->cap_extra_temps = mc->cap_extra_temps ? mc->cap_extra_temps * 2 : 8;
        mc->extra_temps = (ExtraTemp *)realloc(mc->extra_temps, sizeof(ExtraTemp) * (unsigned int)mc->cap_extra_temps);
    }
    mc->extra_temps[mc->n_extra_temps].name = cs_strdup(buf);
    mc->extra_temps[mc->n_extra_temps].ctype = cs_strdup(ctype);
    mc->n_extra_temps++;
    return cs_strdup(buf);
}

static void mc_add_local(MethodCtx *mc, const char *name, CsType *type) {
    if (mc->n_locals >= mc->cap_locals) { mc->cap_locals = mc->cap_locals ? mc->cap_locals * 2 : 8; mc->locals = (LocalVarInfo *)realloc(mc->locals, sizeof(LocalVarInfo) * (unsigned int)mc->cap_locals); }
    mc->locals[mc->n_locals].name = cs_strdup(name);
    mc->locals[mc->n_locals].type = type;
    mc->n_locals++;
}
static CsType *mc_lookup_local(MethodCtx *mc, const char *name) {
    int i;
    for (i = mc->n_locals - 1; i >= 0; i--) if (strcmp(mc->locals[i].name, name) == 0) return mc->locals[i].type;
    return 0;
}

/* Same best-effort resolution as infer_class_type() below, but returns
 * the raw CsType* (not resolved down to a class decl) -- used to detect
 * a List<T> receiver (or any other generic-shaped type) where
 * infer_class_type() would come back NULL (reg_find_class() only knows
 * about concrete, non-generic user classes). The returned CsType is
 * BORROWED (owned by whatever local/field/type-arg it came from) --
 * callers must not free it. */
static CsNode *infer_class_type(CsNode *e, MethodCtx *mc);
static CsType *infer_full_type(CsNode *e, MethodCtx *mc) {
    if (!e) return 0;
    if (e->kind == CS_THIS) return 0; /* `this`'s own type isn't tracked as a CsType anywhere -- callers needing it use infer_class_type instead */
    if (e->kind == CS_IDENT) {
        CsType *t = mc_lookup_local(mc, e->ident.name);
        if (t) return t;
        if (mc->class_decl) {
            CsNode *f = class_find_field(mc->class_decl, e->ident.name);
            if (f) return f->kind == CS_FIELD_DECL ? f->field_decl.type : f->property_decl.type;
        }
        return 0;
    }
    if (e->kind == CS_MEMBER) {
        CsNode *owner = infer_class_type(e->member.obj, mc);
        if (owner) {
            CsNode *f = class_find_field(owner, e->member.name);
            if (f) return f->kind == CS_FIELD_DECL ? f->field_decl.type : f->property_decl.type;
        }
        return 0;
    }
    if (e->kind == CS_NEW_OBJECT) return e->new_object.type;
    if (e->kind == CS_CAST) return e->cast.type;
    return 0;
}

/* True if `t` is a closed List<T> instantiation; `*out_elem` (borrowed)
 * is set to T itself. */
static int type_is_list(CsType *t, CsType **out_elem) {
    if (t && strcmp(t->name, "List") == 0 && t->n_type_args == 1 && t->array_rank == 0) {
        *out_elem = t->type_args[0];
        return 1;
    }
    return 0;
}

/* Best-effort static-type resolution for an expression, used to resolve
 * member/method-call receivers -- NOT full type inference (see
 * cs_lower.h's scope note). Returns the resolved class decl, or NULL
 * (caller reports the "cannot resolve receiver type" error) . */
static CsNode *infer_class_type(CsNode *e, MethodCtx *mc) {
    if (!e) return 0;
    if (e->kind == CS_THIS) return mc->class_decl;
    if (e->kind == CS_IDENT) {
        CsType *t = mc_lookup_local(mc, e->ident.name);
        if (t && t->n_type_args == 0 && t->array_rank == 0) return reg_find_class(mc->reg, t->name);
        if (!t && mc->class_decl) {
            CsNode *f = class_find_field(mc->class_decl, e->ident.name);
            if (f) {
                CsType *ft = f->kind == CS_FIELD_DECL ? f->field_decl.type : f->property_decl.type;
                if (ft && ft->n_type_args == 0 && ft->array_rank == 0) return reg_find_class(mc->reg, ft->name);
            }
        }
        return 0;
    }
    if (e->kind == CS_MEMBER) {
        CsNode *owner = infer_class_type(e->member.obj, mc);
        if (owner) {
            CsNode *f = class_find_field(owner, e->member.name);
            if (f) {
                CsType *ft = f->kind == CS_FIELD_DECL ? f->field_decl.type : f->property_decl.type;
                if (ft && ft->n_type_args == 0 && ft->array_rank == 0) return reg_find_class(mc->reg, ft->name);
            }
        }
        return 0;
    }
    if (e->kind == CS_NEW_OBJECT) return reg_find_class(mc->reg, e->new_object.type->name);
    if (e->kind == CS_CAST) return reg_find_class(mc->reg, e->cast.type->name);
    return 0;
}

/* Phase 6e: same best-effort resolution as infer_class_type() above, but
 * for an expression whose STATIC type is an INTERFACE (a local variable
 * or field declared "IShape shape;", not a class) -- used to decide
 * whether a method call needs real vtable dispatch (lower_call's own use
 * site). Deliberately narrower than infer_class_type: an interface-typed
 * value only ever comes from a local/parameter/field declaration or a
 * cast in this phase's scope (no interface-returning method inference,
 * matching every other "best-effort, not full type inference" limitation
 * already documented throughout this file). */
static CsNode *infer_interface_type(CsNode *e, MethodCtx *mc) {
    if (!e) return 0;
    if (e->kind == CS_IDENT) {
        CsType *t = mc_lookup_local(mc, e->ident.name);
        if (t && t->n_type_args == 0 && t->array_rank == 0) return reg_find_interface(mc->reg, t->name);
        if (!t && mc->class_decl) {
            CsNode *f = class_find_field(mc->class_decl, e->ident.name);
            if (f) {
                CsType *ft = f->kind == CS_FIELD_DECL ? f->field_decl.type : f->property_decl.type;
                if (ft && ft->n_type_args == 0 && ft->array_rank == 0) return reg_find_interface(mc->reg, ft->name);
            }
        }
        return 0;
    }
    if (e->kind == CS_CAST) return reg_find_interface(mc->reg, e->cast.type->name);
    return 0;
}

static void lower_expr(CsNode *e, MethodCtx *mc, StrBuf *out);

static void escape_c_string(const char *s, StrBuf *out) {
    sb_append(out, "\"");
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') { char t[3] = {'\\', *s, 0}; sb_append(out, t); }
        else if (*s == '\n') sb_append(out, "\\n");
        else if (*s == '\t') sb_append(out, "\\t");
        else if (*s == '\r') sb_append(out, "\\r");
        else { char t[2] = { *s, 0 }; sb_append(out, t); }
    }
    sb_append(out, "\"");
}

/* Best-effort "is `e` string-typed?" check, used to decide whether a C#
 * '+' should lower to cs_string_concat() instead of raw C '+' (adding two
 * CsString* pointers would silently compile as pointer arithmetic --
 * wrong, and no diagnostic would ever catch it). NOT real type inference
 * (see cs_lower.h's scope note): literals and locals/fields resolve via
 * infer_full_type(); a '+' chain propagates through itself so "a + b + c"
 * (parsed as "(a + b) + c") correctly treats the inner sum as
 * string-typed too if either of ITS operands was; a call's return type
 * isn't tracked at all, so `SomeMethod() + "x"` is a real, documented gap
 * (the call side is silently treated as already-CsString*, matching
 * lower_expr_as_string's own fallback below). */
static int expr_is_string(CsNode *e, MethodCtx *mc) {
    CsType *t;
    if (!e) return 0;
    if (e->kind == CS_LIT_STRING || e->kind == CS_LIT_INTERP_STRING) return 1;
    if (e->kind == CS_BINARY && strcmp(e->binary.op, "+") == 0) return expr_is_string(e->binary.left, mc) || expr_is_string(e->binary.right, mc);
    if (e->kind == CS_CAST) return e->cast.type && strcmp(e->cast.type->name, "string") == 0;
    t = infer_full_type(e, mc);
    return t && t->n_type_args == 0 && t->array_rank == 0 && strcmp(t->name, "string") == 0;
}

/* Lowers `e` as an expression that must produce a CsString* -- used for
 * string-interpolation slots and Console.WriteLine/Write arguments. Not
 * real type inference (see cs_lower.h): a literal is handled precisely;
 * anything else is assumed to already be CsString*-typed unless its
 * LOCAL declared type (see MethodCtx) is a known non-string primitive,
 * in which case it's wrapped via cs_string_from_int/cs_string_from_double.
 * This is a documented, real limitation -- an expression whose type this
 * heuristic gets wrong will compile to a type-mismatched C expression,
 * which squash's own C type checking may or may not catch. */
static void lower_expr_as_string(CsNode *e, MethodCtx *mc, StrBuf *out) {
    if (e->kind == CS_LIT_STRING || e->kind == CS_LIT_INTERP_STRING) { lower_expr(e, mc, out); return; }
    if (e->kind == CS_LIT_INT) { sb_append(out, "cs_string_from_int("); lower_expr(e, mc, out); sb_append(out, ")"); return; }
    if (e->kind == CS_LIT_DOUBLE) { sb_append(out, "cs_string_from_double("); lower_expr(e, mc, out); sb_append(out, ")"); return; }
    if (e->kind == CS_IDENT) {
        CsType *t = mc_lookup_local(mc, e->ident.name);
        if (t && strcmp(t->name, "int") == 0) { sb_append(out, "cs_string_from_int("); lower_expr(e, mc, out); sb_append(out, ")"); return; }
        if (t && strcmp(t->name, "double") == 0) { sb_append(out, "cs_string_from_double("); lower_expr(e, mc, out); sb_append(out, ")"); return; }
    }
    lower_expr(e, mc, out); /* assume already CsString* */
}

/* Emits a call's arguments against a resolved callee `m`, prefixing "&"
 * onto any argument whose corresponding declared parameter is "out"/
 * "ref" -- real P/Invoke's own by-reference marshaling rule (see
 * lower_class_methods' own comment on the matching [DllImport]
 * declaration side: only meaningful for DllImport methods today, since
 * ordinary C# methods don't implement "out"/"ref" parameter-passing at
 * all yet). `m` may be NULL (ordinary, non-DllImport calls never need
 * this), in which case every argument lowers plain, unprefixed. */
static void lower_call_args(CsNode *e, CsNode *m, MethodCtx *mc, StrBuf *out, int first_arg_needs_comma) {
    int i;
    for (i = 0; i < e->call.argc; i++) {
        int is_out_ref = m && i < m->method_decl.n_params && m->method_decl.params[i]->param.is_out_ref;
        if (i > 0 || first_arg_needs_comma) sb_append(out, ", ");
        if (is_out_ref) sb_append(out, "&");
        lower_expr(e->call.args[i], mc, out);
    }
}

static void lower_call(CsNode *e, MethodCtx *mc, StrBuf *out) {
    CsNode *callee = e->call.callee;
    int i;
    if (callee->kind == CS_MEMBER) {
        CsNode *obj = callee->member.obj;
        const char *mname = callee->member.name;
        if (obj->kind == CS_IDENT && strcmp(obj->ident.name, "Console") == 0 && !mc_lookup_local(mc, "Console")) {
            if (strcmp(mname, "WriteLine") == 0 || strcmp(mname, "Write") == 0) {
                sb_append(out, strcmp(mname, "WriteLine") == 0 ? "csr_console_write_line(" : "csr_console_write(");
                if (e->call.argc == 0) sb_append(out, "cs_string_new(\"\")");
                else lower_expr_as_string(e->call.args[0], mc, out);
                sb_append(out, ")");
                return;
            }
            lower_error(e->line, "unsupported Console method '%s'", mname);
            sb_append(out, "((void)0)");
            return;
        }
        {
            CsType *list_t = infer_full_type(obj, mc);
            CsType *elem;
            if (type_is_list(list_t, &elem)) {
                if (strcmp(mname, "Add") == 0 && e->call.argc == 1) {
                    char *ct = lower_type_str(elem, mc->reg, e->line);
                    char *tmp = mc_new_temp(mc, ct);
                    sb_appendf(out, "(%s = ", tmp);
                    lower_expr(e->call.args[0], mc, out);
                    sb_append(out, ", csr_list_add(");
                    lower_expr(obj, mc, out);
                    sb_appendf(out, ", &%s))", tmp);
                    free(ct); free(tmp);
                    return;
                }
                if (strcmp(mname, "RemoveAt") == 0 && e->call.argc == 1) {
                    sb_append(out, "csr_list_remove_at(");
                    lower_expr(obj, mc, out);
                    sb_append(out, ", ");
                    lower_expr(e->call.args[0], mc, out);
                    sb_append(out, ")");
                    return;
                }
                if (strcmp(mname, "Clear") == 0 && e->call.argc == 0) {
                    sb_append(out, "csr_list_clear(");
                    lower_expr(obj, mc, out);
                    sb_append(out, ")");
                    return;
                }
                lower_error(e->line, "unsupported List<T> method '%s'", mname);
                sb_append(out, "((void)0)");
                return;
            }
        }
        /* Static-class-qualified call: Obj is a known class name, not a local variable. */
        if (obj->kind == CS_IDENT && !mc_lookup_local(mc, obj->ident.name)) {
            CsNode *cls = reg_find_class(mc->reg, obj->ident.name);
            if (cls) {
                CsNode *m = class_find_method(cls, mname);
                if (m && m->method_decl.is_static) {
                    /* Phase 6c: a [DllImport] method calls straight
                     * through to its real native name, not the usual
                     * "Class__Method" mangling -- see lower_class_methods'
                     * own comment on the matching declaration side. */
                    if (m->method_decl.dllimport_name) {
                        sb_appendf(out, "%s(", mname);
                        lower_call_args(e, m, mc, out, 0);
                    } else {
                        sb_appendf(out, "%s__%s(", cls->class_decl.name, mname);
                        for (i = 0; i < e->call.argc; i++) { if (i) sb_append(out, ", "); lower_expr(e->call.args[i], mc, out); }
                    }
                    sb_append(out, ")");
                    return;
                }
                lower_error(e->line, "'%s.%s' is not a known static method", obj->ident.name, mname);
                sb_append(out, "((void)0)");
                return;
            }
        }
        {
            /* Phase 6e: an interface-typed receiver dispatches through
             * that interface's real vtable, resolved at RUNTIME by the
             * object's own type id -- genuine dynamic dispatch, unlike
             * every other call shape in this function (all compile-time-
             * resolved to one fixed "ClassName__Method" function). Must
             * be checked before infer_class_type() below: an interface-
             * typed local's declared type name is never a registered
             * CLASS, so infer_class_type would just return NULL for it
             * and fall into the "cannot resolve receiver type" error. */
            CsNode *iface = infer_interface_type(obj, mc);
            if (iface) {
                CsNode *im = class_find_method(iface, mname);
                if (!im) { lower_error(e->line, "'%s' has no method '%s'", iface->class_decl.name, mname); sb_append(out, "((void)0)"); return; }
                sb_appendf(out, "csr_vtable_for_%s(csr_type_id_of(", iface->class_decl.name);
                lower_expr(obj, mc, out);
                sb_appendf(out, "))->%s(", mname);
                lower_expr(obj, mc, out);
                for (i = 0; i < e->call.argc; i++) { sb_append(out, ", "); lower_expr(e->call.args[i], mc, out); }
                sb_append(out, ")");
                return;
            }
        }
        {
            CsNode *owner = infer_class_type(obj, mc);
            CsNode *m;
            if (!owner) { lower_error(e->line, "cannot resolve receiver type for call to '.%s(...)' (no local type inference for this expression shape)", mname); sb_append(out, "((void)0)"); return; }
            m = class_find_method(owner, mname);
            if (!m) { lower_error(e->line, "'%s' has no method '%s'", owner->class_decl.name, mname); sb_append(out, "((void)0)"); return; }
            sb_appendf(out, "%s__%s(", owner->class_decl.name, mname);
            /* Struct-typed receivers lower to plain C values (see
             * lower_type_str's own comment), but every instance method's
             * "this" is still a pointer -- take its address here, same as
             * ordinary C# "ref this" semantics. Class-typed receivers are
             * already GC pointers, passed as-is. */
            if (owner->kind == CS_STRUCT_DECL) sb_append(out, "&");
            lower_expr(obj, mc, out);
            for (i = 0; i < e->call.argc; i++) { sb_append(out, ", "); lower_expr(e->call.args[i], mc, out); }
            sb_append(out, ")");
            return;
        }
    }
    if (callee->kind == CS_IDENT) {
        const char *name = callee->ident.name;
        CsNode *m = mc->class_decl ? class_find_method(mc->class_decl, name) : 0;
        if (m) {
            if (m->method_decl.dllimport_name) {
                sb_appendf(out, "%s(", name);
                lower_call_args(e, m, mc, out, 0);
            } else {
                sb_appendf(out, "%s__%s(", mc->class_decl->class_decl.name, name);
                if (!m->method_decl.is_static) { sb_append(out, "this"); if (e->call.argc) sb_append(out, ", "); }
                for (i = 0; i < e->call.argc; i++) { if (i) sb_append(out, ", "); lower_expr(e->call.args[i], mc, out); }
            }
            sb_append(out, ")");
            return;
        }
        lower_error(e->line, "call to unknown function/method '%s'", name);
        sb_append(out, "((void)0)");
        return;
    }
    lower_error(e->line, "unsupported call expression shape");
    sb_append(out, "((void)0)");
}

static void lower_expr(CsNode *e, MethodCtx *mc, StrBuf *out) {
    if (!e) return;
    switch (e->kind) {
    case CS_LIT_INT: sb_appendf(out, "%lldLL", e->lit_int.value); return;
    case CS_LIT_DOUBLE: sb_appendf(out, "%g", e->lit_double.value); return;
    case CS_LIT_STRING: sb_append(out, "cs_string_new("); escape_c_string(e->lit_string.value, out); sb_append(out, ")"); return;
    case CS_LIT_BOOL: sb_append(out, e->lit_bool.value ? "1" : "0"); return;
    case CS_LIT_NULL: sb_append(out, "0"); return;
    case CS_LIT_CHAR: sb_appendf(out, "((char)%lld)", e->lit_char.value); return;
    case CS_LIT_INTERP_STRING: {
        int i;
        if (e->lit_interp.n_parts == 0) { sb_append(out, "cs_string_new(\"\")"); return; }
        for (i = 1; i < e->lit_interp.n_parts; i++) sb_append(out, "cs_string_concat(");
        {
            CsInterpPart *p0 = &e->lit_interp.parts[0];
            if (p0->is_expr) lower_expr_as_string(p0->expr, mc, out);
            else { sb_append(out, "cs_string_new("); escape_c_string(p0->text, out); sb_append(out, ")"); }
        }
        for (i = 1; i < e->lit_interp.n_parts; i++) {
            CsInterpPart *pi = &e->lit_interp.parts[i];
            sb_append(out, ", ");
            if (pi->is_expr) lower_expr_as_string(pi->expr, mc, out);
            else { sb_append(out, "cs_string_new("); escape_c_string(pi->text, out); sb_append(out, ")"); }
            sb_append(out, ")");
        }
        return;
    }
    case CS_IDENT: {
        if (mc_lookup_local(mc, e->ident.name)) { sb_append(out, e->ident.name); return; }
        if (mc->class_decl) {
            CsNode *f = class_find_field(mc->class_decl, e->ident.name);
            if (f) {
                if (mc->is_instance) sb_appendf(out, "this->%s", e->ident.name);
                else sb_appendf(out, "%s__%s", mc->class_decl->class_decl.name, e->ident.name);
                return;
            }
            {
                /* Phase 6d: a bare static method NAME used as a VALUE, not
                 * called (e.g. "RegisterCallback(MyCallback)") -- a real,
                 * C-ABI-compatible function pointer for native callback
                 * registration (Vulkan's debug-messenger/allocation-
                 * callback APIs are exactly this shape). Only static
                 * methods: an instance method's real C function also
                 * needs a "this" pointer bound in, which is full
                 * delegate/closure territory (still queued separately,
                 * see cs_lower.h's own scope notes) -- a static method
                 * has no such need, its "ClassName__Method" C function is
                 * ALREADY the real, complete function value, exactly like
                 * a plain C function name decaying to a pointer. Not
                 * reached for the callee of an actual call expression
                 * (CS_CALL) -- lower_call() handles that shape itself,
                 * directly, without going through lower_expr on the
                 * callee node at all. */
                CsNode *m = class_find_method(mc->class_decl, e->ident.name);
                if (m && m->method_decl.is_static) {
                    if (m->method_decl.dllimport_name) sb_append(out, e->ident.name);
                    else sb_appendf(out, "%s__%s", mc->class_decl->class_decl.name, e->ident.name);
                    return;
                }
            }
        }
        sb_append(out, e->ident.name); /* best-effort fallback -- see header comment */
        return;
    }
    case CS_THIS: sb_append(out, "this"); return;
    case CS_BASE: sb_append(out, "this"); return; /* see cs_lower.h: base-member access approximated as this */
    case CS_ASSIGN: {
        const char *op = e->assign.op;
        if (strcmp(op, "?\?=") == 0) { lower_error(e->line, "the null-coalescing-assignment operator is not supported yet"); sb_append(out, "((void)0)"); return; }
        if (e->assign.lhs->kind == CS_INDEX) {
            CsType *list_t = infer_full_type(e->assign.lhs->index_.obj, mc);
            CsType *elem;
            if (type_is_list(list_t, &elem)) {
                if (strcmp(op, "=") != 0) { lower_error(e->line, "compound assignment to a List<T> element is not supported yet, only '='"); sb_append(out, "((void)0)"); return; }
                {
                    char *ct = lower_type_str(elem, mc->reg, e->line);
                    char *tmp = mc_new_temp(mc, ct);
                    sb_appendf(out, "(%s = ", tmp);
                    lower_expr(e->assign.rhs, mc, out);
                    sb_append(out, ", csr_list_set(");
                    lower_expr(e->assign.lhs->index_.obj, mc, out);
                    sb_append(out, ", ");
                    lower_expr(e->assign.lhs->index_.index, mc, out);
                    sb_appendf(out, ", &%s))", tmp);
                    free(ct); free(tmp);
                    return;
                }
            }
        }
        sb_append(out, "(");
        lower_expr(e->assign.lhs, mc, out);
        sb_appendf(out, " %s ", op);
        lower_expr(e->assign.rhs, mc, out);
        sb_append(out, ")");
        return;
    }
    case CS_BINARY: {
        if (strcmp(e->binary.op, "is") == 0 || strcmp(e->binary.op, "as") == 0) {
            /* NOTE: uses csr_type_id_of(), not an inline "csr_hdr_of(x)->
             * type_id" -- see that function's own comment in csharp_rt.h
             * for the real squash codegen bug this sidesteps (a pointer-
             * returning function's result, dereferenced with "->field"
             * INLINE inside a larger expression, reads back a wrong value
             * when the pointed-to struct mixes pointer and int fields,
             * exactly CsObjHeader's shape). csr_type_id_of() already
             * null-checks internally, so "is" only needs ONE evaluation
             * of the left operand (also fixing a real, separate bug: the
             * previous version textually duplicated `left`, double-
             * evaluating any side effects it has -- wrong regardless of
             * the codegen bug). "as" still evaluates `left` twice
             * (documented limitation: real C# guarantees single
             * evaluation; this lowering doesn't yet, since C has no
             * expression-local temporary without relying on GNU
             * statement-expressions, which haven't been verified safe
             * under squash and are avoided here on principle after this
             * session's bug count). */
            const char *tname = e->binary.right->ident.name; /* see cs_parser.c: is/as RHS is a plain type-name CS_IDENT */
            CsNode *cls = reg_find_class(mc->reg, tname);
            int tid = cls ? reg_type_id(mc->reg, cls) : -1;
            if (strcmp(e->binary.op, "is") == 0) {
                sb_appendf(out, "(csr_type_id_of((void*)(");
                lower_expr(e->binary.left, mc, out);
                sb_appendf(out, ")) == %d)", tid);
            } else {
                sb_appendf(out, "((csr_type_id_of((void*)(");
                lower_expr(e->binary.left, mc, out);
                sb_appendf(out, ")) == %d) ? (%s*)(", tid, tname);
                lower_expr(e->binary.left, mc, out);
                sb_append(out, ") : 0)");
            }
            return;
        }
        if (strcmp(e->binary.op, "+") == 0 && (expr_is_string(e->binary.left, mc) || expr_is_string(e->binary.right, mc))) {
            sb_append(out, "cs_string_concat(");
            lower_expr_as_string(e->binary.left, mc, out);
            sb_append(out, ", ");
            lower_expr_as_string(e->binary.right, mc, out);
            sb_append(out, ")");
            return;
        }
        sb_append(out, "(");
        lower_expr(e->binary.left, mc, out);
        sb_appendf(out, " %s ", e->binary.op);
        lower_expr(e->binary.right, mc, out);
        sb_append(out, ")");
        return;
    }
    case CS_UNARY: {
        if (e->unary.postfix) { sb_append(out, "("); lower_expr(e->unary.operand, mc, out); sb_appendf(out, "%s)", e->unary.op); }
        else { sb_appendf(out, "(%s", e->unary.op); lower_expr(e->unary.operand, mc, out); sb_append(out, ")"); }
        return;
    }
    case CS_TERNARY:
        sb_append(out, "(");
        lower_expr(e->ternary.cond, mc, out);
        sb_append(out, " ? ");
        lower_expr(e->ternary.then_, mc, out);
        sb_append(out, " : ");
        lower_expr(e->ternary.else_, mc, out);
        sb_append(out, ")");
        return;
    case CS_CALL: lower_call(e, mc, out); return;
    case CS_MEMBER: {
        CsNode *owner;
        CsType *full_t = infer_full_type(e->member.obj, mc);
        CsType *elem;
        if (type_is_list(full_t, &elem) && strcmp(e->member.name, "Count") == 0) {
            sb_append(out, "csr_list_count(");
            lower_expr(e->member.obj, mc, out);
            sb_append(out, ")");
            return;
        }
        if (full_t && strcmp(full_t->name, "string") == 0 && strcmp(e->member.name, "Length") == 0) {
            sb_append(out, "(");
            lower_expr(e->member.obj, mc, out);
            sb_append(out, ")->len");
            return;
        }
        /* Phase 6d: "ClassName.Method" used as a VALUE (not called) --
         * same static-method-as-real-function-pointer case as the
         * unqualified CS_IDENT branch above, just class-qualified. Must
         * be checked BEFORE infer_class_type(e->member.obj, ...) below:
         * "obj" here is a class NAME, not a variable/field of some class
         * TYPE, so infer_class_type (which only resolves a variable's/
         * field's own declared type) would never recognize it -- this
         * mirrors lower_call()'s own "Static-class-qualified call" check
         * for the exact same "Obj is a known class name" shape. */
        if (e->member.obj->kind == CS_IDENT && !mc_lookup_local(mc, e->member.obj->ident.name)) {
            CsNode *cls = reg_find_class(mc->reg, e->member.obj->ident.name);
            if (cls) {
                CsNode *m = class_find_method(cls, e->member.name);
                if (m && m->method_decl.is_static) {
                    if (m->method_decl.dllimport_name) sb_append(out, e->member.name);
                    else sb_appendf(out, "%s__%s", cls->class_decl.name, e->member.name);
                    return;
                }
            }
        }
        owner = infer_class_type(e->member.obj, mc);
        if (owner) {
            CsNode *f = class_find_field(owner, e->member.name);
            if (f) {
                /* Struct-typed objects lower to plain C VALUES (see
                 * lower_type_str's own comment), so field access is
                 * "." -- class-typed objects are still GC pointers, so
                 * "->" as before. */
                const char *op = (owner->kind == CS_STRUCT_DECL) ? "." : "->";
                sb_append(out, "(");
                lower_expr(e->member.obj, mc, out);
                sb_appendf(out, ")%s%s", op, e->member.name);
                return;
            }
        }
        lower_error(e->line, "cannot resolve member access '.%s'", e->member.name);
        sb_append(out, "0");
        return;
    }
    case CS_INDEX: {
        CsType *list_t = infer_full_type(e->index_.obj, mc);
        CsType *elem;
        if (type_is_list(list_t, &elem)) {
            char *ct = lower_type_str(elem, mc->reg, e->line);
            char *tmp = mc_new_temp(mc, ct);
            sb_appendf(out, "(csr_list_get(");
            lower_expr(e->index_.obj, mc, out);
            sb_append(out, ", ");
            lower_expr(e->index_.index, mc, out);
            sb_appendf(out, ", &%s), %s)", tmp, tmp);
            free(ct); free(tmp);
            return;
        }
        lower_error(e->line, "indexing/arrays are not supported yet outside List<T> -- tracked follow-up, see cs_lower.h");
        sb_append(out, "0");
        return;
    }
    case CS_NEW_OBJECT: {
        CsNode *cls;
        CsType *elem;
        int i;
        if (type_is_list(e->new_object.type, &elem)) {
            char *ct = lower_type_str(elem, mc->reg, e->line);
            sb_appendf(out, "csr_list_new((int)sizeof(%s), 4)", ct);
            free(ct);
            return;
        }
        cls = reg_find_class(mc->reg, e->new_object.type->name);
        if (!cls) { lower_error(e->line, "unknown type '%s' in 'new'", e->new_object.type->name); sb_append(out, "0"); return; }
        if (cls->kind == CS_STRUCT_DECL) {
            /* Value-type construction: no GC allocation -- the ctor now
             * takes the struct's address as an explicit first parameter
             * and fills it in place (see lower_class_methods()'s own
             * comment on the struct-ctor signature). "new StructType(...)"
             * used as an EXPRESSION needs a real addressable temporary to
             * hand the ctor a pointer to -- same hoisted-temp/comma-
             * operator technique already established for List<T>.Add()
             * etc (see MethodCtx.extra_temps' own comment): the whole
             * expression evaluates to the temp itself afterward, which is
             * a real struct VALUE usable anywhere (assignment, a further
             * function argument, ...). */
            char *tmp = mc_new_temp(mc, cls->class_decl.name);
            sb_appendf(out, "(%s__ctor(&%s", cls->class_decl.name, tmp);
            for (i = 0; i < e->new_object.argc; i++) { sb_append(out, ", "); lower_expr(e->new_object.args[i], mc, out); }
            sb_appendf(out, "), %s)", tmp);
            free(tmp);
            return;
        }
        sb_appendf(out, "%s__ctor(", cls->class_decl.name);
        for (i = 0; i < e->new_object.argc; i++) { if (i) sb_append(out, ", "); lower_expr(e->new_object.args[i], mc, out); }
        sb_append(out, ")");
        return;
    }
    case CS_NEW_ARRAY:
        lower_error(e->line, "array creation is not supported yet -- tracked follow-up, see cs_lower.h");
        sb_append(out, "0");
        return;
    case CS_LAMBDA:
        lower_error(e->line, "lambdas/closures are not supported yet -- tracked follow-up, see cs_lower.h");
        sb_append(out, "0");
        return;
    case CS_CAST: {
        char *ct = lower_type_str(e->cast.type, mc->reg, e->line);
        sb_appendf(out, "((%s)(", ct);
        lower_expr(e->cast.expr, mc, out);
        sb_append(out, "))");
        free(ct);
        return;
    }
    default:
        lower_error(e->line, "internal: unhandled expression kind %d in lower_expr", (int)e->kind);
        sb_append(out, "0");
        return;
    }
}

static void lower_stmt(CsNode *s, MethodCtx *mc, StrBuf *out, int indent);

static void ind(StrBuf *out, int n) { int i; for (i = 0; i < n; i++) sb_append(out, "    "); }

static void lower_local_var_decl(CsNode *s, MethodCtx *mc, StrBuf *out, int indent, int with_semicolon) {
    CsType *resolved = s->local_var_decl.type;
    char *ct;
    ind(out, indent);
    if (s->local_var_decl.is_var) {
        if (!s->local_var_decl.init) { lower_error(s->line, "'var %s' needs an initializer", s->local_var_decl.name); sb_append(out, "void *"); sb_append(out, s->local_var_decl.name); }
        else if (s->local_var_decl.init->kind == CS_NEW_OBJECT) resolved = s->local_var_decl.init->new_object.type;
        else if (s->local_var_decl.init->kind == CS_LIT_INT) resolved = cstype_new("int");
        else if (s->local_var_decl.init->kind == CS_LIT_DOUBLE) resolved = cstype_new("double");
        else if (s->local_var_decl.init->kind == CS_LIT_STRING || s->local_var_decl.init->kind == CS_LIT_INTERP_STRING) resolved = cstype_new("string");
        else if (s->local_var_decl.init->kind == CS_LIT_BOOL) resolved = cstype_new("bool");
        else { lower_error(s->line, "cannot infer 'var' type for '%s' from this initializer shape", s->local_var_decl.name); resolved = cstype_new("int"); }
    }
    if (resolved) {
        ct = lower_type_str(resolved, mc->reg, s->line);
        sb_appendf(out, "%s %s", ct, s->local_var_decl.name);
        free(ct);
        mc_add_local(mc, s->local_var_decl.name, resolved);
    }
    if (s->local_var_decl.init) { sb_append(out, " = "); lower_expr(s->local_var_decl.init, mc, out); }
    if (with_semicolon) sb_append(out, ";\n");
}

static void lower_stmt(CsNode *s, MethodCtx *mc, StrBuf *out, int indent) {
    if (!s) return;
    switch (s->kind) {
    case CS_BLOCK: {
        int i;
        ind(out, indent); sb_append(out, "{\n");
        for (i = 0; i < s->block.n_stmts; i++) lower_stmt(s->block.stmts[i], mc, out, indent + 1);
        ind(out, indent); sb_append(out, "}\n");
        return;
    }
    case CS_IF:
        ind(out, indent); sb_append(out, "if (");
        lower_expr(s->if_.cond, mc, out);
        sb_append(out, ")\n");
        lower_stmt(s->if_.then_, mc, out, indent);
        if (s->if_.else_) { ind(out, indent); sb_append(out, "else\n"); lower_stmt(s->if_.else_, mc, out, indent); }
        return;
    case CS_FOR:
        ind(out, indent); sb_append(out, "for (");
        if (s->for_.init) {
            if (s->for_.init->kind == CS_LOCAL_VAR_DECL) lower_local_var_decl(s->for_.init, mc, out, 0, 0);
            else lower_expr(s->for_.init->expr_stmt.expr, mc, out);
        }
        sb_append(out, "; ");
        if (s->for_.cond) lower_expr(s->for_.cond, mc, out);
        sb_append(out, "; ");
        if (s->for_.step) lower_expr(s->for_.step, mc, out);
        sb_append(out, ")\n");
        lower_stmt(s->for_.body, mc, out, indent);
        return;
    case CS_FOREACH: {
        CsType *coll_t = infer_full_type(s->foreach_.collection, mc);
        CsType *elem;
        if (!type_is_list(coll_t, &elem)) {
            lower_error(s->line, "foreach is only supported over a List<T> right now (not arrays or other IEnumerable<T>) -- tracked follow-up, see cs_lower.h");
            return;
        }
        {
            char *ct = lower_type_str(s->foreach_.elem_type ? s->foreach_.elem_type : elem, mc->reg, s->line);
            int id = mc->temp_counter++;
            ind(out, indent); sb_appendf(out, "{\n");
            ind(out, indent + 1); sb_appendf(out, "CsList *__iter%d = ", id);
            lower_expr(s->foreach_.collection, mc, out);
            sb_append(out, ";\n");
            ind(out, indent + 1); sb_appendf(out, "int __n%d = csr_list_count(__iter%d);\n", id, id);
            ind(out, indent + 1); sb_appendf(out, "int __idx%d;\n", id);
            ind(out, indent + 1); sb_appendf(out, "for (__idx%d = 0; __idx%d < __n%d; __idx%d = __idx%d + 1) {\n", id, id, id, id, id);
            ind(out, indent + 2); sb_appendf(out, "%s %s;\n", ct, s->foreach_.var_name);
            ind(out, indent + 2); sb_appendf(out, "csr_list_get(__iter%d, __idx%d, &%s);\n", id, id, s->foreach_.var_name);
            {
                CsType *loop_var_type = s->foreach_.elem_type ? s->foreach_.elem_type : elem;
                mc_add_local(mc, s->foreach_.var_name, loop_var_type);
            }
            lower_stmt(s->foreach_.body, mc, out, indent + 2);
            ind(out, indent + 1); sb_append(out, "}\n");
            ind(out, indent); sb_append(out, "}\n");
            free(ct);
        }
        return;
    }
    case CS_WHILE:
        ind(out, indent); sb_append(out, "while (");
        lower_expr(s->while_.cond, mc, out);
        sb_append(out, ")\n");
        lower_stmt(s->while_.body, mc, out, indent);
        return;
    case CS_DO_WHILE:
        ind(out, indent); sb_append(out, "do\n");
        lower_stmt(s->do_while.body, mc, out, indent);
        ind(out, indent); sb_append(out, "while (");
        lower_expr(s->do_while.cond, mc, out);
        sb_append(out, ");\n");
        return;
    case CS_SWITCH: {
        int i, j;
        ind(out, indent); sb_append(out, "switch (");
        lower_expr(s->switch_.expr, mc, out);
        sb_append(out, ") {\n");
        for (i = 0; i < s->switch_.n_cases; i++) {
            CsNode *c = s->switch_.cases[i];
            if (c->switch_case.is_default) { ind(out, indent); sb_append(out, "default:\n"); }
            for (j = 0; j < c->switch_case.n_values; j++) {
                ind(out, indent); sb_append(out, "case ");
                lower_expr(c->switch_case.values[j], mc, out);
                sb_append(out, ":\n");
            }
            for (j = 0; j < c->switch_case.n_stmts; j++) lower_stmt(c->switch_case.stmts[j], mc, out, indent + 1);
        }
        ind(out, indent); sb_append(out, "}\n");
        return;
    }
    case CS_BREAK: ind(out, indent); sb_append(out, "break;\n"); return;
    case CS_CONTINUE: ind(out, indent); sb_append(out, "continue;\n"); return;
    case CS_RETURN:
        ind(out, indent); sb_append(out, "return");
        if (s->return_.expr) { sb_append(out, " "); lower_expr(s->return_.expr, mc, out); }
        sb_append(out, ";\n");
        return;
    case CS_TRY:
        lower_error(s->line, "try/catch/finally is not supported yet -- tracked follow-up, see cs_lower.h");
        return;
    case CS_THROW:
        lower_error(s->line, "throw is not supported yet -- tracked follow-up, see cs_lower.h");
        return;
    case CS_LOCAL_VAR_DECL:
        lower_local_var_decl(s, mc, out, indent, 1);
        return;
    case CS_EXPR_STMT:
        ind(out, indent);
        lower_expr(s->expr_stmt.expr, mc, out);
        sb_append(out, ";\n");
        return;
    default:
        lower_error(s->line, "internal: unhandled statement kind %d in lower_stmt", (int)s->kind);
        return;
    }
}

/* ---- class/member lowering ---- */

static void lower_struct_decl(CsNode *cls, ClassRegistry *reg, StrBuf *out) {
    int i;
    int n_fields = 0;
    sb_appendf(out, "typedef struct %s {\n", cls->class_decl.name);
    for (i = 0; i < cls->class_decl.n_members; i++) {
        CsNode *m = cls->class_decl.members[i];
        CsType *ft = 0; const char *fname = 0; int is_static = 0;
        if (m->kind == CS_FIELD_DECL) { ft = m->field_decl.type; fname = m->field_decl.name; is_static = m->field_decl.is_static; }
        else if (m->kind == CS_PROPERTY_DECL) { ft = m->property_decl.type; fname = m->property_decl.name; is_static = m->property_decl.is_static; }
        if (ft && !is_static) {
            char *ct = lower_type_str(ft, reg, m->line);
            sb_appendf(out, "    %s %s;\n", ct, fname);
            free(ct);
            n_fields++;
        }
    }
    /* A class with only methods/static members (e.g. "class Program {
     * static void Main() ... }") has no instance fields at all -- a
     * zero-sized C struct is invalid, so give it one placeholder field.
     * Harmless: nothing ever reads it, and csr_gc_alloc's real size still
     * comes from sizeof(this struct), which is at least 1 byte either way. */
    if (n_fields == 0) sb_append(out, "    int __unused;\n");
    sb_appendf(out, "} %s;\n\n", cls->class_decl.name);
}

/* Phase 6e: emits one "typedef struct { RetType (*Method)(void *self,
 * Args...); ... } IfaceName_VTable;" per interface -- the real dispatch
 * mechanism (not a type-switch): a class implementing this interface
 * gets its own "static const IfaceName_VTable ClassName__IfaceName_
 * vtable = { ...ClassName's own matching functions... };" (see
 * lower_interface_impls() below), and calling an interface-typed
 * receiver's method resolves the right vtable at runtime by the
 * object's own type id (see lower_call()'s own dispatch-emission site)
 * -- genuinely dynamic dispatch, not compile-time-resolved to one
 * class's function the way a plain class-typed call already is.
 *
 * "self" is untyped ("void*") in the vtable's own function-pointer
 * field type, since a single struct type must describe every
 * implementing class's function uniformly even though each class's
 * REAL function takes its own concrete "ClassName *this" first
 * parameter -- lower_interface_impls() casts each function pointer to
 * this exact signature when building the vtable literal, the standard
 * C vtable-emulation technique (real per-class dispatch through a
 * uniform pointer-sized "self" is well past what strict ISO C function-
 * pointer compatibility rules allow, but is exactly how every C-based
 * OOP vtable, including this project's own compiler internals in spirit,
 * has always actually worked in practice). No return-type-covariance or
 * default-interface-method support -- every interface method must be a
 * plain abstract signature, matching cs_parser.c's own existing "body ==
 * NULL" handling for an interface member. */
static void lower_interface_vtable_structs(ClassRegistry *reg, StrBuf *out) {
    int i;
    for (i = 0; i < reg->n_interfaces; i++) {
        CsNode *iface = reg->interfaces[i];
        int j;
        sb_appendf(out, "typedef struct {\n");
        for (j = 0; j < iface->class_decl.n_members; j++) {
            CsNode *m = iface->class_decl.members[j];
            char *rt; int k;
            if (m->kind != CS_METHOD_DECL) continue;
            rt = lower_type_str(m->method_decl.ret_type, reg, m->line);
            sb_appendf(out, "    %s (*%s)(void *self", rt, m->method_decl.name);
            free(rt);
            for (k = 0; k < m->method_decl.n_params; k++) {
                char *pt = lower_type_str(m->method_decl.params[k]->param.type, reg, m->line);
                sb_appendf(out, ", %s", pt);
                free(pt);
            }
            sb_append(out, ");\n");
        }
        sb_appendf(out, "} %s_VTable;\n\n", iface->class_decl.name);
        /* Forward declaration for the real csr_vtable_for_<Iface>()
         * dispatch function -- its FULL DEFINITION (lower_interface_
         * dispatch()) has to come after every class's methods and vtable
         * instance are emitted (needs their addresses), but any method
         * call through an interface-typed receiver anywhere in the
         * program (potentially emitted much earlier -- e.g. inside
         * "static void Main()", if Main happens to be declared before
         * the classes it uses, which real C# allows freely) needs a
         * visible prototype before that point. Squash's own C parser is
         * lenient about a forward call with no visible prototype at all
         * (treats it as an implicit external declaration, resolved via
         * same-TU direct-call label fixup) -- but this project holds
         * every cs_lower.c fixture to gcc-vs-squash PARITY (see this
         * file's whole test-fixture convention), and plain gcc genuinely
         * rejects this: an implicit-declaration function defaults to
         * returning "int", which doesn't match this function's real
         * "const IfaceName_VTable *" return type, a hard type-conflict
         * error at the real definition site later in the same file
         * (confirmed via a direct repro before this forward declaration
         * was added). */
        sb_appendf(out, "static const %s_VTable *csr_vtable_for_%s(int type_id);\n\n", iface->class_decl.name, iface->class_decl.name);
    }
}

/* For every class implementing 1+ interfaces: verify (a lower_error, not
 * a silent skip, if not) it defines every interface method by name+arity,
 * then emit its real vtable -- a "static const" struct of function
 * pointers pointing at that class's own matching "ClassName__Method"
 * functions, each cast to the vtable field's uniform "(RetType
 * (*)(void*, Args...))" signature (see lower_interface_vtable_structs()'s
 * own comment on why the cast is needed and safe in practice). Must run
 * AFTER lower_class_methods() has emitted every class's real method
 * function definitions (referencing a function by name before its own
 * definition needs at least a prior prototype, which plain instance
 * methods don't get here -- simplest to just order this pass after). */
static void lower_interface_impls(ClassRegistry *reg, StrBuf *out) {
    int i;
    for (i = 0; i < reg->n_classes; i++) {
        CsNode *cls = reg->classes[i];
        /* candidate interface names: base_class_name (if it turns out to
         * actually be an interface -- see class_implements()'s own
         * comment on why the parser can't tell) followed by
         * interface_names[]. */
        int n_cand = cls->class_decl.n_interfaces + (cls->class_decl.base_class_name ? 1 : 0);
        int ii;
        for (ii = 0; ii < n_cand; ii++) {
            const char *cand_name = (ii == 0 && cls->class_decl.base_class_name)
                ? cls->class_decl.base_class_name
                : cls->class_decl.interface_names[ii - (cls->class_decl.base_class_name ? 1 : 0)];
            CsNode *iface = reg_find_interface(reg, cand_name);
            int j;
            if (!iface) {
                /* base_class_name not resolving to a registered interface
                 * just means it's a real (still-unsupported) base CLASS
                 * name -- not an error here, that's a separate, already-
                 * documented gap, not this function's concern. Only an
                 * explicit interface_names[] entry that fails to resolve
                 * is a real error (a class can't list something in a ","-
                 * separated interface list that isn't a known interface). */
                if (ii == 0 && cls->class_decl.base_class_name && cand_name == cls->class_decl.base_class_name) continue;
                lower_error(cls->line, "'%s' implements unknown interface '%s'", cls->class_decl.name, cand_name);
                continue;
            }
            sb_appendf(out, "static const %s_VTable %s__%s_vtable = {\n",
                       iface->class_decl.name, cls->class_decl.name, iface->class_decl.name);
            for (j = 0; j < iface->class_decl.n_members; j++) {
                CsNode *im = iface->class_decl.members[j];
                CsNode *cm; char *rt; int k;
                if (im->kind != CS_METHOD_DECL) continue;
                cm = class_find_method(cls, im->method_decl.name);
                if (!cm || cm->method_decl.n_params != im->method_decl.n_params) {
                    lower_error(cls->line, "'%s' does not implement '%s.%s' (interface method missing or wrong parameter count)",
                                cls->class_decl.name, iface->class_decl.name, im->method_decl.name);
                    sb_append(out, "    0,\n");
                    continue;
                }
                rt = lower_type_str(im->method_decl.ret_type, reg, im->line);
                sb_appendf(out, "    (%s (*)(void*", rt);
                free(rt);
                for (k = 0; k < im->method_decl.n_params; k++) {
                    char *pt = lower_type_str(im->method_decl.params[k]->param.type, reg, im->line);
                    sb_appendf(out, ", %s", pt);
                    free(pt);
                }
                sb_appendf(out, "))%s__%s,\n", cls->class_decl.name, im->method_decl.name);
            }
            sb_append(out, "};\n\n");
        }
    }
}

/* One dispatch function per interface: "static const IfaceName_VTable
 * *csr_vtable_for_IfaceName(int type_id)" -- a plain switch over every
 * class in THIS program implementing the interface, returning that
 * class's own vtable instance (or NULL if `type_id` implements no such
 * interface, e.g. a stale/foreign type id -- callers are expected to
 * only ever pass a type id that's already known to implement the
 * interface, matching every other best-effort-not-fully-checked
 * assumption in this lowering pass, but NULL is safer than an
 * out-of-bounds table read if that assumption is ever violated). Real,
 * runtime dynamic dispatch (decided by the object's ACTUAL type id, read
 * fresh at each call via csr_type_id_of()) -- not resolved at compile
 * time to one fixed class's function, unlike an ordinary class-typed
 * method call. */
static void lower_interface_dispatch(ClassRegistry *reg, StrBuf *out) {
    int i;
    for (i = 0; i < reg->n_interfaces; i++) {
        CsNode *iface = reg->interfaces[i];
        int ci;
        sb_appendf(out, "static const %s_VTable *csr_vtable_for_%s(int type_id) {\n    switch (type_id) {\n",
                   iface->class_decl.name, iface->class_decl.name);
        for (ci = 0; ci < reg->n_classes; ci++) {
            CsNode *cls = reg->classes[ci];
            if (class_implements(cls, iface->class_decl.name, reg))
                sb_appendf(out, "    case %d: return &%s__%s_vtable;\n", reg_type_id(reg, cls), cls->class_decl.name, iface->class_decl.name);
        }
        sb_append(out, "    default: return 0;\n    }\n}\n\n");
    }
}

static void lower_static_fields(CsNode *cls, ClassRegistry *reg, StrBuf *out) {
    int i;
    for (i = 0; i < cls->class_decl.n_members; i++) {
        CsNode *m = cls->class_decl.members[i];
        if (m->kind == CS_FIELD_DECL && m->field_decl.is_static) {
            char *ct = lower_type_str(m->field_decl.type, reg, m->line);
            sb_appendf(out, "%s %s__%s", ct, cls->class_decl.name, m->field_decl.name);
            free(ct);
            if (m->field_decl.init) {
                MethodCtx mc; memset(&mc, 0, sizeof mc); mc.reg = reg; mc.class_decl = cls;
                sb_append(out, " = ");
                lower_expr(m->field_decl.init, &mc, out);
            }
            sb_append(out, ";\n");
        }
    }
}

static void lower_method_params(CsNode *method_or_ctor, int is_ctor, ClassRegistry *reg, MethodCtx *mc, StrBuf *out, int need_this, const char *this_type) {
    CsNode **params; int n_params; int i; int wrote_any = 0;
    if (is_ctor) { params = method_or_ctor->ctor_decl.params; n_params = method_or_ctor->ctor_decl.n_params; }
    else { params = method_or_ctor->method_decl.params; n_params = method_or_ctor->method_decl.n_params; }
    sb_append(out, "(");
    if (need_this) { sb_appendf(out, "%s *this", this_type); wrote_any = 1; }
    for (i = 0; i < n_params; i++) {
        char *ct = lower_type_str(params[i]->param.type, reg, params[i]->line);
        if (wrote_any) sb_append(out, ", ");
        sb_appendf(out, "%s %s", ct, params[i]->param.name);
        free(ct);
        mc_add_local(mc, params[i]->param.name, params[i]->param.type);
        wrote_any = 1;
    }
    if (!wrote_any) sb_append(out, "void");
    sb_append(out, ")");
}

static void lower_field_initializers(CsNode *cls, MethodCtx *mc, StrBuf *out) {
    int i;
    for (i = 0; i < cls->class_decl.n_members; i++) {
        CsNode *m = cls->class_decl.members[i];
        if (m->kind == CS_FIELD_DECL && !m->field_decl.is_static && m->field_decl.init) {
            sb_appendf(out, "    this->%s = ", m->field_decl.name);
            lower_expr(m->field_decl.init, mc, out);
            sb_append(out, ";\n");
        }
    }
}

static void lower_class_methods(CsNode *cls, ClassRegistry *reg, StrBuf *out) {
    int i;
    int n_ctors = 0;
    for (i = 0; i < cls->class_decl.n_members; i++) if (cls->class_decl.members[i]->kind == CS_CTOR_DECL) n_ctors++;
    if (n_ctors > 1) lower_error(cls->line, "class '%s' has %d constructors -- constructor overload resolution is not supported yet, only one ctor per class", cls->class_decl.name, n_ctors);

    {
        CsNode *ctor = class_find_ctor(cls);
        MethodCtx mc; StrBuf body; int k;
        int is_struct = (cls->kind == CS_STRUCT_DECL);
        memset(&mc, 0, sizeof mc); mc.reg = reg; mc.class_decl = cls; mc.is_instance = 1;
        if (is_struct) {
            /* Value-type ctor: "this" is an explicit, caller-owned
             * (address-of-a-real-storage-location, typically a hoisted
             * temp -- see CS_NEW_OBJECT's own comment) OUT parameter, not
             * a GC allocation -- lower_method_params(need_this=1) already
             * emits it as the first parameter (same as any instance
             * method), so no separate "%s *this = csr_gc_alloc(...)" line
             * is needed here at all. Zeroed first (memset), matching real
             * C#'s "every struct field is implicitly zero before the
             * ctor body runs" rule, before field initializers/ctor body
             * (which may not touch every field) run. */
            sb_appendf(out, "void %s__ctor", cls->class_decl.name);
            lower_method_params(ctor ? ctor : 0, 1, reg, &mc, out, 1, cls->class_decl.name);
            sb_append(out, " {\n");
            sb_appendf(out, "    memset(this, 0, sizeof(*this));\n");
        } else {
            sb_appendf(out, "%s *%s__ctor", cls->class_decl.name, cls->class_decl.name);
            if (ctor) lower_method_params(ctor, 1, reg, &mc, out, 0, 0);
            else sb_append(out, "(void)");
            sb_append(out, " {\n");
            sb_appendf(out, "    %s *this = (%s*)csr_gc_alloc(sizeof(%s), %d, CS_KIND_OBJECT);\n",
                       cls->class_decl.name, cls->class_decl.name, cls->class_decl.name, reg_type_id(reg, cls));
        }
        /* body lowered into a scratch buffer FIRST so mc.extra_temps (see
         * ExtraTemp's own comment) is fully populated before we know what
         * hoisted temp declarations to emit -- they go right after the
         * "this = csr_gc_alloc(...)"/"memset(this, ...)" line above,
         * before anything that might reference them. */
        sb_init(&body);
        lower_field_initializers(cls, &mc, &body);
        if (ctor) {
            int j;
            for (j = 0; j < ctor->ctor_decl.body->block.n_stmts; j++) lower_stmt(ctor->ctor_decl.body->block.stmts[j], &mc, &body, 1);
        }
        for (k = 0; k < mc.n_extra_temps; k++) sb_appendf(out, "    %s %s;\n", mc.extra_temps[k].ctype, mc.extra_temps[k].name);
        sb_append(out, body.data);
        free(body.data);
        if (is_struct) sb_append(out, "}\n\n");
        else sb_append(out, "    return this;\n}\n\n");
    }

    for (i = 0; i < cls->class_decl.n_members; i++) {
        CsNode *m = cls->class_decl.members[i];
        if (m->kind != CS_METHOD_DECL) continue;
        {
            MethodCtx mc; char *rt; StrBuf body; int k;
            memset(&mc, 0, sizeof mc); mc.reg = reg; mc.class_decl = cls; mc.is_instance = !m->method_decl.is_static;
            if (m->method_decl.n_type_params > 0) { lower_error(m->line, "generic methods are not supported yet ('%s')", m->method_decl.name); continue; }
            if (m->method_decl.dllimport_name) {
                /* Phase 6c: "[DllImport("lib")] static extern <ret>
                 * Name(...);" -- declares a REAL native symbol (the
                 * method's own C# name, matching .NET's own default
                 * EntryPoint-defaults-to-method-name rule), not a mangled
                 * "Class__Method" function -- every call to this method
                 * (see lower_call's matching dllimport_name check) routes
                 * straight to it by that real name. This emits only the
                 * "extern <ret> <name>(<types>);" declaration itself --
                 * compiling with "-c" turns this bodyless call into a
                 * RELOC_STATIC_REL32 relocation resolved by NAME at load
                 * time (see SQW/sqo_loader.c's host symbol table), the
                 * same existing mechanism every CSR/csharp_rt.h call
                 * already uses -- no new loader machinery needed, just
                 * more host-symbol-table entries (e.g.
                 * SQW/sqo_host_syms.c's sqo_host_syms_vulkan()). */
                rt = lower_type_str(m->method_decl.ret_type, reg, m->line);
                sb_appendf(out, "extern %s %s(", rt, m->method_decl.name);
                free(rt);
                /* "out"/"ref" parameters marshal as a real pointer to the
                 * caller's storage (real P/Invoke's own rule -- e.g.
                 * Vulkan's "vkEnumerateInstanceVersion(uint32_t
                 * *pApiVersion)" needs a real uint32_t*, not a uint32_t
                 * value copy); every call to this method (see
                 * lower_call's own dllimport_name handling below) passes
                 * "&argexpr" for these positions to match. Not routed
                 * through lower_method_params() -- that helper's
                 * pointer-vs-value choice is driven entirely by the C#
                 * TYPE (struct vs class vs primitive), which has no
                 * concept of "out"/"ref"; simpler to lower this one,
                 * DllImport-specific shape directly here than to thread a
                 * new parameter through every other lower_method_params
                 * call site. */
                {
                    int j; int wrote_any = 0;
                    for (j = 0; j < m->method_decl.n_params; j++) {
                        CsNode *pr = m->method_decl.params[j];
                        char *ct = lower_type_str(pr->param.type, reg, pr->line);
                        if (wrote_any) sb_append(out, ", ");
                        sb_appendf(out, "%s %s%s", ct, pr->param.is_out_ref ? "*" : "", pr->param.name);
                        free(ct);
                        wrote_any = 1;
                    }
                    if (!wrote_any) sb_append(out, "void");
                }
                sb_append(out, ");\n\n");
                continue;
            }
            rt = lower_type_str(m->method_decl.ret_type, reg, m->line);
            sb_appendf(out, "%s %s__%s", rt, cls->class_decl.name, m->method_decl.name);
            free(rt);
            lower_method_params(m, 0, reg, &mc, out, mc.is_instance, cls->class_decl.name);
            if (!m->method_decl.body) { sb_append(out, ";\n\n"); continue; }
            sb_append(out, " {\n");
            sb_init(&body);
            {
                int j;
                for (j = 0; j < m->method_decl.body->block.n_stmts; j++) lower_stmt(m->method_decl.body->block.stmts[j], &mc, &body, 1);
            }
            for (k = 0; k < mc.n_extra_temps; k++) sb_appendf(out, "    %s %s;\n", mc.extra_temps[k].ctype, mc.extra_temps[k].name);
            sb_append(out, body.data);
            free(body.data);
            sb_append(out, "}\n\n");
        }
    }
}

/* ---- top-level orchestration ---- */

static CsNode *find_main(ClassRegistry *reg, CsNode **out_class) {
    int i;
    for (i = 0; i < reg->n_classes; i++) {
        CsNode *m = class_find_method(reg->classes[i], "Main");
        if (m && m->method_decl.is_static) { *out_class = reg->classes[i]; return m; }
    }
    return 0;
}

CsLowerResult cs_lower_unit(CsNode *unit, const char *runtime_header) {
    CsLowerResult res;
    ClassRegistry reg;
    StrBuf out;
    int i;
    CsNode *main_class = 0;
    CsNode *main_method;

    memset(&reg, 0, sizeof reg);
    reg.type_id_next = 1;
    g_error_count = 0;

    for (i = 0; i < unit->unit.n_decls; i++) collect_classes(unit->unit.decls[i], &reg);

    sb_init(&out);
    sb_appendf(&out, "#include \"%s\"\n\n", runtime_header);

    for (i = 0; i < reg.n_classes; i++) lower_struct_decl(reg.classes[i], &reg, &out);
    lower_interface_vtable_structs(&reg, &out);
    for (i = 0; i < reg.n_classes; i++) lower_static_fields(reg.classes[i], &reg, &out);
    sb_append(&out, "\n");
    for (i = 0; i < reg.n_classes; i++) lower_class_methods(reg.classes[i], &reg, &out);
    /* Phase 6e: must run AFTER lower_class_methods() -- see
     * lower_interface_impls()'s own comment on why. */
    lower_interface_impls(&reg, &out);
    lower_interface_dispatch(&reg, &out);

    /* lower_class_methods() above already emitted "<MainClass>__Main(void) {
     * ... }" as an ordinary static method -- the real C entry point below
     * just calls it, rather than re-lowering (and so double-reporting any
     * errors in) the same body a second time. */
    main_method = find_main(&reg, &main_class);
    if (!main_method) {
        lower_error(unit->line, "no 'static void Main()' (or static Main with a return value) found in the program");
    } else {
        sb_appendf(&out, "int main(void) {\n    %s__Main();\n    return 0;\n}\n", main_class->class_decl.name);
    }

    res.text = out.data;
    res.error_count = g_error_count;
    res.ok = (g_error_count == 0);
    free(reg.classes);
    free(reg.type_ids);
    free(reg.interfaces);
    return res;
}
