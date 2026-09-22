#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpp_ast.h"

char *cpp_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    memcpy(r, s, n);
    return r;
}

static CppNode *mk(CppAstKind kind, int line) {
    CppNode *n = calloc(1, sizeof(CppNode));
    n->kind = kind;
    n->line = line;
    return n;
}

CppType *cpptype_new(const char *name) {
    CppType *t = calloc(1, sizeof(CppType));
    t->name = cpp_strdup(name);
    return t;
}

CppType *cpptype_copy(const CppType *t) {
    if (!t) return NULL;
    CppType *c = cpptype_new(t->name);
    c->ptr_depth = t->ptr_depth;
    c->is_ref = t->is_ref;
    c->is_const = t->is_const;
    c->n_type_args = t->n_type_args;
    if (t->n_type_args > 0) {
        c->type_args = malloc(sizeof(CppType*) * t->n_type_args);
        for (int i = 0; i < t->n_type_args; i++) c->type_args[i] = cpptype_copy(t->type_args[i]);
    }
    return c;
}

void cpptype_free(CppType *t) {
    if (!t) return;
    for (int i = 0; i < t->n_type_args; i++) cpptype_free(t->type_args[i]);
    free(t->type_args);
    free(t->name);
    free(t);
}

/* Stable-ish mangled suffix for a type, used both for overload resolution
 * bucket names and template instantiation names — e.g. "int" -> "int",
 * "Point*" -> "Point_p", "std::vector<int>" -> "std__vector__int". Kept
 * to [A-Za-z0-9_] only so it's always a legal C identifier fragment. */
char *cpptype_mangle(const CppType *t) {
    char buf[256];
    buf[0] = '\0';
    if (!t) return cpp_strdup("void");
    size_t off = 0;
    for (const char *p = t->name; *p && off < sizeof(buf) - 1; p++) {
        if (*p == ':') { if (off < sizeof(buf) - 1) buf[off++] = '_'; }
        else buf[off++] = *p;
    }
    buf[off] = '\0';
    if (t->n_type_args > 0) {
        for (int i = 0; i < t->n_type_args; i++) {
            char *sub = cpptype_mangle(t->type_args[i]);
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "__%s", sub);
            free(sub);
        }
    }
    for (int i = 0; i < t->ptr_depth; i++)
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "_p");
    return cpp_strdup(buf);
}

CppNode *cppnode_lit_int(long long v, int line) { CppNode *n = mk(CPP_LIT_INT, line); n->lit_int.value = v; return n; }
CppNode *cppnode_lit_double(double v, int line) { CppNode *n = mk(CPP_LIT_DOUBLE, line); n->lit_double.value = v; return n; }
CppNode *cppnode_lit_string(const char *v, int line) { CppNode *n = mk(CPP_LIT_STRING, line); n->lit_string.value = cpp_strdup(v); return n; }
CppNode *cppnode_lit_char(long long v, int line) { CppNode *n = mk(CPP_LIT_CHAR, line); n->lit_char.value = v; return n; }
CppNode *cppnode_lit_bool(int v, int line) { CppNode *n = mk(CPP_LIT_BOOL, line); n->lit_bool.value = v; return n; }
CppNode *cppnode_lit_nullptr(int line) { return mk(CPP_LIT_NULLPTR, line); }
CppNode *cppnode_ident(const char *name, int line) { CppNode *n = mk(CPP_IDENT, line); n->ident.name = cpp_strdup(name); return n; }
CppNode *cppnode_this(int line) { return mk(CPP_THIS, line); }

CppNode *cppnode_assign(const char *op, CppNode *lhs, CppNode *rhs, int line) {
    CppNode *n = mk(CPP_ASSIGN, line); n->assign.op = cpp_strdup(op); n->assign.lhs = lhs; n->assign.rhs = rhs; return n;
}
CppNode *cppnode_binary(const char *op, CppNode *l, CppNode *r, int line) {
    CppNode *n = mk(CPP_BINARY, line); n->binary.op = cpp_strdup(op); n->binary.left = l; n->binary.right = r; return n;
}
CppNode *cppnode_unary(const char *op, CppNode *operand, int postfix, int line) {
    CppNode *n = mk(CPP_UNARY, line); n->unary.op = cpp_strdup(op); n->unary.operand = operand; n->unary.postfix = postfix; return n;
}
CppNode *cppnode_ternary(CppNode *c, CppNode *t, CppNode *e, int line) {
    CppNode *n = mk(CPP_TERNARY, line); n->ternary.cond = c; n->ternary.then_ = t; n->ternary.else_ = e; return n;
}
CppNode *cppnode_call(CppNode *callee, CppNode **args, int argc, int line) {
    CppNode *n = mk(CPP_CALL, line); n->call.callee = callee; n->call.args = args; n->call.argc = argc; return n;
}
CppNode *cppnode_member(CppNode *obj, const char *name, int line) {
    CppNode *n = mk(CPP_MEMBER, line); n->member.obj = obj; n->member.name = cpp_strdup(name); return n;
}
CppNode *cppnode_arrow(CppNode *obj, const char *name, int line) {
    CppNode *n = mk(CPP_ARROW, line); n->arrow.obj = obj; n->arrow.name = cpp_strdup(name); return n;
}
CppNode *cppnode_index(CppNode *obj, CppNode *index, int line) {
    CppNode *n = mk(CPP_INDEX, line); n->index_.obj = obj; n->index_.index = index; return n;
}
CppNode *cppnode_scope(const char *qualifier, const char *name, int line) {
    CppNode *n = mk(CPP_SCOPE, line); n->scope.qualifier = cpp_strdup(qualifier); n->scope.name = cpp_strdup(name); return n;
}
CppNode *cppnode_new(CppType *type, CppNode **args, int argc, int line) {
    CppNode *n = mk(CPP_NEW, line); n->new_.type = type; n->new_.args = args; n->new_.argc = argc; return n;
}
CppNode *cppnode_new_array(CppType *type, CppNode *size_expr, int line) {
    CppNode *n = mk(CPP_NEW_ARRAY, line); n->new_array.type = type; n->new_array.size_expr = size_expr; return n;
}
CppNode *cppnode_delete(CppNode *expr, int line) { CppNode *n = mk(CPP_DELETE, line); n->delete_.expr = expr; return n; }
CppNode *cppnode_delete_array(CppNode *expr, int line) { CppNode *n = mk(CPP_DELETE_ARRAY, line); n->delete_array.expr = expr; return n; }
CppNode *cppnode_cast(CppType *type, CppNode *expr, int line) {
    CppNode *n = mk(CPP_CAST, line); n->cast.type = type; n->cast.expr = expr; return n;
}
CppNode *cppnode_sizeof_type(CppType *type, int line) { CppNode *n = mk(CPP_SIZEOF, line); n->sizeof_.type = type; return n; }
CppNode *cppnode_sizeof_expr(CppNode *expr, int line) { CppNode *n = mk(CPP_SIZEOF, line); n->sizeof_.expr = expr; return n; }

CppNode *cppnode_block(CppNode **stmts, int n_stmts, int line) {
    CppNode *n = mk(CPP_BLOCK, line); n->block.stmts = stmts; n->block.n_stmts = n_stmts; return n;
}
CppNode *cppnode_if(CppNode *cond, CppNode *then_, CppNode *else_, int line) {
    CppNode *n = mk(CPP_IF, line); n->if_.cond = cond; n->if_.then_ = then_; n->if_.else_ = else_; return n;
}
CppNode *cppnode_for(CppNode *init, CppNode *cond, CppNode *step, CppNode *body, int line) {
    CppNode *n = mk(CPP_FOR, line); n->for_.init = init; n->for_.cond = cond; n->for_.step = step; n->for_.body = body; return n;
}
CppNode *cppnode_while(CppNode *cond, CppNode *body, int line) {
    CppNode *n = mk(CPP_WHILE, line); n->while_.cond = cond; n->while_.body = body; return n;
}
CppNode *cppnode_do_while(CppNode *body, CppNode *cond, int line) {
    CppNode *n = mk(CPP_DO_WHILE, line); n->do_while.body = body; n->do_while.cond = cond; return n;
}
CppNode *cppnode_switch(CppNode *expr, CppNode **cases, int n_cases, int line) {
    CppNode *n = mk(CPP_SWITCH, line); n->switch_.expr = expr; n->switch_.cases = cases; n->switch_.n_cases = n_cases; return n;
}
CppNode *cppnode_switch_case(CppNode *value, CppNode **stmts, int n_stmts, int line) {
    CppNode *n = mk(CPP_SWITCH_CASE, line); n->switch_case.value = value; n->switch_case.stmts = stmts; n->switch_case.n_stmts = n_stmts; return n;
}
CppNode *cppnode_break(int line) { return mk(CPP_BREAK, line); }
CppNode *cppnode_continue(int line) { return mk(CPP_CONTINUE, line); }
CppNode *cppnode_return(CppNode *expr, int line) { CppNode *n = mk(CPP_RETURN, line); n->return_.expr = expr; return n; }
CppNode *cppnode_var_decl(CppType *type, const char *name, CppNode *init, int line) {
    CppNode *n = mk(CPP_VAR_DECL, line); n->var_decl.type = type; n->var_decl.name = cpp_strdup(name); n->var_decl.init = init; return n;
}
CppNode *cppnode_expr_stmt(CppNode *expr, int line) { CppNode *n = mk(CPP_EXPR_STMT, line); n->expr_stmt.expr = expr; return n; }

CppNode *cppnode_unit(CppNode **decls, int n_decls, int line) {
    CppNode *n = mk(CPP_UNIT, line); n->unit.decls = decls; n->unit.n_decls = n_decls; return n;
}
CppNode *cppnode_using_namespace(const char *name, int line) {
    CppNode *n = mk(CPP_USING_NAMESPACE, line); n->using_namespace.name = cpp_strdup(name); return n;
}
CppNode *cppnode_using_decl(const char *qualified_name, int line) {
    CppNode *n = mk(CPP_USING_DECL, line); n->using_decl.qualified_name = cpp_strdup(qualified_name); return n;
}
CppNode *cppnode_namespace(const char *name, CppNode **decls, int n_decls, int line) {
    CppNode *n = mk(CPP_NAMESPACE, line); n->namespace_.name = cpp_strdup(name); n->namespace_.decls = decls; n->namespace_.n_decls = n_decls; return n;
}
CppNode *cppnode_class_decl(const char *name, int is_struct, const char *base_class_name,
                             CppNode **template_params, int n_template_params,
                             CppNode **members, int n_members, int line) {
    CppNode *n = mk(CPP_CLASS_DECL, line);
    n->class_decl.name = cpp_strdup(name);
    n->class_decl.is_struct = is_struct;
    n->class_decl.base_class_name = base_class_name ? cpp_strdup(base_class_name) : NULL;
    n->class_decl.template_params = template_params; n->class_decl.n_template_params = n_template_params;
    n->class_decl.members = members; n->class_decl.n_members = n_members;
    return n;
}
CppNode *cppnode_field_decl(CppType *type, const char *name, CppNode *init, CppAccess access, int is_static, int line) {
    CppNode *n = mk(CPP_FIELD_DECL, line);
    n->field_decl.type = type; n->field_decl.name = cpp_strdup(name); n->field_decl.init = init;
    n->field_decl.access = access; n->field_decl.is_static = is_static;
    return n;
}
CppNode *cppnode_method_decl(CppType *ret_type, const char *name, CppNode **params, int n_params,
                              CppNode *body, CppAccess access, int is_static, int is_virtual, int is_const,
                              const char *op_name, int line) {
    CppNode *n = mk(CPP_METHOD_DECL, line);
    n->method_decl.ret_type = ret_type; n->method_decl.name = cpp_strdup(name);
    n->method_decl.params = params; n->method_decl.n_params = n_params;
    n->method_decl.body = body; n->method_decl.access = access;
    n->method_decl.is_static = is_static; n->method_decl.is_virtual = is_virtual; n->method_decl.is_const = is_const;
    n->method_decl.op_name = op_name ? cpp_strdup(op_name) : NULL;
    return n;
}
CppNode *cppnode_ctor_decl(const char *name, CppNode **params, int n_params, CppNode *body,
                            CppNode **init_list_args, int n_init_args, const char *init_list_target,
                            CppAccess access, int line) {
    CppNode *n = mk(CPP_CTOR_DECL, line);
    n->ctor_decl.name = cpp_strdup(name); n->ctor_decl.params = params; n->ctor_decl.n_params = n_params;
    n->ctor_decl.body = body;
    n->ctor_decl.init_list_args = init_list_args; n->ctor_decl.n_init_args = n_init_args;
    n->ctor_decl.init_list_target = init_list_target ? cpp_strdup(init_list_target) : NULL;
    n->ctor_decl.access = access;
    return n;
}
CppNode *cppnode_dtor_decl(const char *name, CppNode *body, CppAccess access, int is_virtual, int line) {
    CppNode *n = mk(CPP_DTOR_DECL, line);
    n->dtor_decl.name = cpp_strdup(name); n->dtor_decl.body = body; n->dtor_decl.access = access; n->dtor_decl.is_virtual = is_virtual;
    return n;
}
CppNode *cppnode_func_decl(CppType *ret_type, const char *name, CppNode **template_params, int n_template_params,
                            CppNode **params, int n_params, CppNode *body, int line) {
    CppNode *n = mk(CPP_FUNC_DECL, line);
    n->func_decl.ret_type = ret_type; n->func_decl.name = cpp_strdup(name);
    n->func_decl.template_params = template_params; n->func_decl.n_template_params = n_template_params;
    n->func_decl.params = params; n->func_decl.n_params = n_params; n->func_decl.body = body;
    return n;
}
CppNode *cppnode_param(CppType *type, const char *name, CppNode *default_value, int line) {
    CppNode *n = mk(CPP_PARAM, line); n->param.type = type; n->param.name = cpp_strdup(name); n->param.default_value = default_value;
    return n;
}
CppNode *cppnode_template_param(const char *name, int line) {
    CppNode *n = mk(CPP_TEMPLATE_PARAM, line); n->template_param.name = cpp_strdup(name); return n;
}
CppNode *cppnode_include_raw(const char *text, int line) {
    CppNode *n = mk(CPP_INCLUDE_RAW, line); n->include_raw.text = cpp_strdup(text); return n;
}

static void free_arr(CppNode **arr, int n) {
    if (!arr) return;
    for (int i = 0; i < n; i++) cppast_free(arr[i]);
    free(arr);
}

void cppast_free(CppNode *n) {
    if (!n) return;
    switch (n->kind) {
        case CPP_LIT_STRING: free(n->lit_string.value); break;
        case CPP_IDENT: free(n->ident.name); break;
        case CPP_ASSIGN: free(n->assign.op); cppast_free(n->assign.lhs); cppast_free(n->assign.rhs); break;
        case CPP_BINARY: free(n->binary.op); cppast_free(n->binary.left); cppast_free(n->binary.right); break;
        case CPP_UNARY: free(n->unary.op); cppast_free(n->unary.operand); break;
        case CPP_TERNARY: cppast_free(n->ternary.cond); cppast_free(n->ternary.then_); cppast_free(n->ternary.else_); break;
        case CPP_CALL: cppast_free(n->call.callee); free_arr(n->call.args, n->call.argc); break;
        case CPP_MEMBER: cppast_free(n->member.obj); free(n->member.name); break;
        case CPP_ARROW: cppast_free(n->arrow.obj); free(n->arrow.name); break;
        case CPP_INDEX: cppast_free(n->index_.obj); cppast_free(n->index_.index); break;
        case CPP_SCOPE: free(n->scope.qualifier); free(n->scope.name); break;
        case CPP_NEW: cpptype_free(n->new_.type); free_arr(n->new_.args, n->new_.argc); break;
        case CPP_NEW_ARRAY: cpptype_free(n->new_array.type); cppast_free(n->new_array.size_expr); break;
        case CPP_DELETE: cppast_free(n->delete_.expr); break;
        case CPP_DELETE_ARRAY: cppast_free(n->delete_array.expr); break;
        case CPP_CAST: cpptype_free(n->cast.type); cppast_free(n->cast.expr); break;
        case CPP_SIZEOF: cpptype_free(n->sizeof_.type); cppast_free(n->sizeof_.expr); break;
        case CPP_BLOCK: free_arr(n->block.stmts, n->block.n_stmts); break;
        case CPP_IF: cppast_free(n->if_.cond); cppast_free(n->if_.then_); cppast_free(n->if_.else_); break;
        case CPP_FOR: cppast_free(n->for_.init); cppast_free(n->for_.cond); cppast_free(n->for_.step); cppast_free(n->for_.body); break;
        case CPP_WHILE: cppast_free(n->while_.cond); cppast_free(n->while_.body); break;
        case CPP_DO_WHILE: cppast_free(n->do_while.body); cppast_free(n->do_while.cond); break;
        case CPP_SWITCH: cppast_free(n->switch_.expr); free_arr(n->switch_.cases, n->switch_.n_cases); break;
        case CPP_SWITCH_CASE: cppast_free(n->switch_case.value); free_arr(n->switch_case.stmts, n->switch_case.n_stmts); break;
        case CPP_RETURN: cppast_free(n->return_.expr); break;
        case CPP_VAR_DECL: cpptype_free(n->var_decl.type); free(n->var_decl.name); cppast_free(n->var_decl.init); break;
        case CPP_EXPR_STMT: cppast_free(n->expr_stmt.expr); break;
        case CPP_UNIT: free_arr(n->unit.decls, n->unit.n_decls); break;
        case CPP_USING_NAMESPACE: free(n->using_namespace.name); break;
        case CPP_USING_DECL: free(n->using_decl.qualified_name); break;
        case CPP_NAMESPACE: free(n->namespace_.name); free_arr(n->namespace_.decls, n->namespace_.n_decls); break;
        case CPP_CLASS_DECL:
            free(n->class_decl.name); free(n->class_decl.base_class_name);
            free_arr(n->class_decl.template_params, n->class_decl.n_template_params);
            free_arr(n->class_decl.members, n->class_decl.n_members);
            break;
        case CPP_FIELD_DECL: cpptype_free(n->field_decl.type); free(n->field_decl.name); cppast_free(n->field_decl.init); break;
        case CPP_METHOD_DECL:
            cpptype_free(n->method_decl.ret_type); free(n->method_decl.name);
            free_arr(n->method_decl.params, n->method_decl.n_params); cppast_free(n->method_decl.body);
            free(n->method_decl.op_name);
            break;
        case CPP_CTOR_DECL:
            free(n->ctor_decl.name); free_arr(n->ctor_decl.params, n->ctor_decl.n_params); cppast_free(n->ctor_decl.body);
            free_arr(n->ctor_decl.init_list_args, n->ctor_decl.n_init_args); free(n->ctor_decl.init_list_target);
            break;
        case CPP_DTOR_DECL: free(n->dtor_decl.name); cppast_free(n->dtor_decl.body); break;
        case CPP_FUNC_DECL:
            cpptype_free(n->func_decl.ret_type); free(n->func_decl.name);
            free_arr(n->func_decl.template_params, n->func_decl.n_template_params);
            free_arr(n->func_decl.params, n->func_decl.n_params); cppast_free(n->func_decl.body);
            break;
        case CPP_PARAM: cpptype_free(n->param.type); free(n->param.name); cppast_free(n->param.default_value); break;
        case CPP_TEMPLATE_PARAM: free(n->template_param.name); break;
        case CPP_INCLUDE_RAW: free(n->include_raw.text); break;
        default: break;
    }
    free(n);
}
