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
typedef struct { CsNode **classes; int n_classes; int type_id_next; int *type_ids; } ClassRegistry;

static void collect_classes(CsNode *decl, ClassRegistry *reg) {
    if (!decl) return;
    if (decl->kind == CS_CLASS_DECL || decl->kind == CS_STRUCT_DECL) {
        reg->classes = (CsNode **)realloc(reg->classes, sizeof(CsNode *) * (unsigned int)(reg->n_classes + 1));
        reg->type_ids = (int *)realloc(reg->type_ids, sizeof(int) * (unsigned int)(reg->n_classes + 1));
        reg->type_ids[reg->n_classes] = reg->type_id_next++;
        reg->classes[reg->n_classes] = decl;
        reg->n_classes++;
    } else if (decl->kind == CS_NAMESPACE) {
        int i;
        for (i = 0; i < decl->namespace_decl.n_decls; i++) collect_classes(decl->namespace_decl.decls[i], reg);
    }
    /* CS_INTERFACE_DECL deliberately not registered as a concrete lowerable
     * type in this phase -- interfaces carry no fields/bodies to lower;
     * virtual dispatch through an interface reference is documented as
     * out of scope (see cs_lower.h). */
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
    if (t->n_type_args > 0) { lower_error(line, "generic types are not supported yet ('%s<...>') -- tracked follow-up, see cs_lower.h", t->name); return cs_strdup("void*"); }
    prim = primitive_c_type(t->name);
    if (prim) return cs_strdup(prim);
    {
        CsNode *cls = reg_find_class(reg, t->name);
        if (cls) {
            char buf[256];
            snprintf(buf, sizeof buf, "%s*", t->name);
            return cs_strdup(buf);
        }
    }
    lower_error(line, "unknown type '%s'", t->name);
    return cs_strdup("void*");
}

/* ---- per-method local-variable static-type tracking (see cs_lower.h) ---- */
typedef struct { char *name; CsType *type; } LocalVarInfo;
typedef struct {
    LocalVarInfo *locals; int n_locals; int cap_locals;
    CsNode *class_decl;     /* enclosing class, or NULL for a free function (not used yet) */
    int is_instance;        /* 1 = has a real `this` */
    ClassRegistry *reg;
} MethodCtx;

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
        /* Static-class-qualified call: Obj is a known class name, not a local variable. */
        if (obj->kind == CS_IDENT && !mc_lookup_local(mc, obj->ident.name)) {
            CsNode *cls = reg_find_class(mc->reg, obj->ident.name);
            if (cls) {
                CsNode *m = class_find_method(cls, mname);
                if (m && m->method_decl.is_static) {
                    sb_appendf(out, "%s__%s(", cls->class_decl.name, mname);
                    for (i = 0; i < e->call.argc; i++) { if (i) sb_append(out, ", "); lower_expr(e->call.args[i], mc, out); }
                    sb_append(out, ")");
                    return;
                }
                lower_error(e->line, "'%s.%s' is not a known static method", obj->ident.name, mname);
                sb_append(out, "((void)0)");
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
            sb_appendf(out, "%s__%s(", mc->class_decl->class_decl.name, name);
            if (!m->method_decl.is_static) { sb_append(out, "this"); if (e->call.argc) sb_append(out, ", "); }
            for (i = 0; i < e->call.argc; i++) { if (i) sb_append(out, ", "); lower_expr(e->call.args[i], mc, out); }
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
        }
        sb_append(out, e->ident.name); /* best-effort fallback -- see header comment */
        return;
    }
    case CS_THIS: sb_append(out, "this"); return;
    case CS_BASE: sb_append(out, "this"); return; /* see cs_lower.h: base-member access approximated as this */
    case CS_ASSIGN: {
        const char *op = e->assign.op;
        if (strcmp(op, "?\?=") == 0) { lower_error(e->line, "the null-coalescing-assignment operator is not supported yet"); sb_append(out, "((void)0)"); return; }
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
        if (e->member.obj->kind == CS_IDENT && strcmp(e->member.obj->ident.name, "string") != 0) {
            CsType *t = mc_lookup_local(mc, e->member.obj->ident.name);
            (void)t;
        }
        owner = infer_class_type(e->member.obj, mc);
        if (owner) {
            CsNode *f = class_find_field(owner, e->member.name);
            if (f) { sb_append(out, "("); lower_expr(e->member.obj, mc, out); sb_appendf(out, ")->%s", e->member.name); return; }
        }
        lower_error(e->line, "cannot resolve member access '.%s'", e->member.name);
        sb_append(out, "0");
        return;
    }
    case CS_INDEX:
        lower_error(e->line, "indexing/arrays are not supported yet -- tracked follow-up, see cs_lower.h");
        sb_append(out, "0");
        return;
    case CS_NEW_OBJECT: {
        CsNode *cls = reg_find_class(mc->reg, e->new_object.type->name);
        int i;
        if (!cls) { lower_error(e->line, "unknown type '%s' in 'new'", e->new_object.type->name); sb_append(out, "0"); return; }
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
    case CS_FOREACH:
        lower_error(s->line, "foreach is not supported yet (tied to array/List support) -- tracked follow-up, see cs_lower.h");
        return;
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
        MethodCtx mc; memset(&mc, 0, sizeof mc); mc.reg = reg; mc.class_decl = cls; mc.is_instance = 1;
        sb_appendf(out, "%s *%s__ctor", cls->class_decl.name, cls->class_decl.name);
        if (ctor) lower_method_params(ctor, 1, reg, &mc, out, 0, 0);
        else sb_append(out, "(void)");
        sb_append(out, " {\n");
        sb_appendf(out, "    %s *this = (%s*)csr_gc_alloc(sizeof(%s), %d, CS_KIND_OBJECT);\n",
                   cls->class_decl.name, cls->class_decl.name, cls->class_decl.name, reg_type_id(reg, cls));
        lower_field_initializers(cls, &mc, out);
        if (ctor) {
            int j;
            for (j = 0; j < ctor->ctor_decl.body->block.n_stmts; j++) lower_stmt(ctor->ctor_decl.body->block.stmts[j], &mc, out, 1);
        }
        sb_append(out, "    return this;\n}\n\n");
    }

    for (i = 0; i < cls->class_decl.n_members; i++) {
        CsNode *m = cls->class_decl.members[i];
        if (m->kind != CS_METHOD_DECL) continue;
        {
            MethodCtx mc; char *rt;
            memset(&mc, 0, sizeof mc); mc.reg = reg; mc.class_decl = cls; mc.is_instance = !m->method_decl.is_static;
            if (m->method_decl.n_type_params > 0) { lower_error(m->line, "generic methods are not supported yet ('%s')", m->method_decl.name); continue; }
            rt = lower_type_str(m->method_decl.ret_type, reg, m->line);
            sb_appendf(out, "%s %s__%s", rt, cls->class_decl.name, m->method_decl.name);
            free(rt);
            lower_method_params(m, 0, reg, &mc, out, mc.is_instance, cls->class_decl.name);
            if (!m->method_decl.body) { sb_append(out, ";\n\n"); continue; }
            sb_append(out, " {\n");
            {
                int j;
                for (j = 0; j < m->method_decl.body->block.n_stmts; j++) lower_stmt(m->method_decl.body->block.stmts[j], &mc, out, 1);
            }
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
    for (i = 0; i < reg.n_classes; i++) lower_static_fields(reg.classes[i], &reg, &out);
    sb_append(&out, "\n");
    for (i = 0; i < reg.n_classes; i++) lower_class_methods(reg.classes[i], &reg, &out);

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
    return res;
}
