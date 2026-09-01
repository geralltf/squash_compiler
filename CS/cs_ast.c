#include "cs_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *cs_strdup(const char *s) {
    char *r;
    unsigned int n;
    if (!s) return 0;
    n = (unsigned int)strlen(s);
    r = (char *)malloc(n + 1);
    memcpy(r, s, n + 1);
    return r;
}

static CsNode *mk(CsAstKind kind, int line) {
    CsNode *n = (CsNode *)malloc(sizeof(CsNode));
    memset(n, 0, sizeof(CsNode));
    n->kind = kind;
    n->line = line;
    return n;
}

/* ---- CsType ---- */
CsType *cstype_new(const char *name) {
    CsType *t = (CsType *)malloc(sizeof(CsType));
    memset(t, 0, sizeof(CsType));
    t->name = cs_strdup(name);
    return t;
}

CsType *cstype_copy(const CsType *t) {
    CsType *out;
    int i;
    if (!t) return 0;
    out = cstype_new(t->name);
    out->array_rank = t->array_rank;
    out->is_nullable = t->is_nullable;
    out->n_type_args = t->n_type_args;
    if (t->n_type_args) {
        out->type_args = (CsType **)malloc(sizeof(CsType *) * (unsigned int)t->n_type_args);
        for (i = 0; i < t->n_type_args; i++) out->type_args[i] = cstype_copy(t->type_args[i]);
    }
    return out;
}

void cstype_free(CsType *t) {
    int i;
    if (!t) return;
    free(t->name);
    for (i = 0; i < t->n_type_args; i++) cstype_free(t->type_args[i]);
    free(t->type_args);
    free(t);
}

char *cstype_str(const CsType *t) {
    char buf[1024];
    int off = 0;
    int i;
    if (!t) return cs_strdup("<null-type>");
    off += snprintf(buf + off, sizeof(buf) - (unsigned int)off, "%s", t->name);
    if (t->n_type_args > 0) {
        off += snprintf(buf + off, sizeof(buf) - (unsigned int)off, "<");
        for (i = 0; i < t->n_type_args; i++) {
            char *sub = cstype_str(t->type_args[i]);
            off += snprintf(buf + off, sizeof(buf) - (unsigned int)off, "%s%s", i ? "," : "", sub);
            free(sub);
        }
        off += snprintf(buf + off, sizeof(buf) - (unsigned int)off, ">");
    }
    for (i = 0; i < t->array_rank; i++) off += snprintf(buf + off, sizeof(buf) - (unsigned int)off, "[]");
    if (t->is_nullable) snprintf(buf + off, sizeof(buf) - (unsigned int)off, "?");
    return cs_strdup(buf);
}

/* ---- constructors ---- */
CsNode *csnode_lit_int(long long v, int line)    { CsNode *n = mk(CS_LIT_INT, line); n->lit_int.value = v; return n; }
CsNode *csnode_lit_double(double v, int line)    { CsNode *n = mk(CS_LIT_DOUBLE, line); n->lit_double.value = v; return n; }
CsNode *csnode_lit_string(const char *v, int line) { CsNode *n = mk(CS_LIT_STRING, line); n->lit_string.value = cs_strdup(v); return n; }
CsNode *csnode_lit_interp(CsInterpPart *parts, int n_parts, int line) {
    CsNode *n = mk(CS_LIT_INTERP_STRING, line); n->lit_interp.parts = parts; n->lit_interp.n_parts = n_parts; return n;
}
CsNode *csnode_lit_bool(int v, int line) { CsNode *n = mk(CS_LIT_BOOL, line); n->lit_bool.value = v; return n; }
CsNode *csnode_lit_null(int line) { return mk(CS_LIT_NULL, line); }
CsNode *csnode_lit_char(long long v, int line) { CsNode *n = mk(CS_LIT_CHAR, line); n->lit_char.value = v; return n; }
CsNode *csnode_ident(const char *name, int line) { CsNode *n = mk(CS_IDENT, line); n->ident.name = cs_strdup(name); return n; }
CsNode *csnode_this(int line) { return mk(CS_THIS, line); }
CsNode *csnode_base(int line) { return mk(CS_BASE, line); }

CsNode *csnode_assign(const char *op, CsNode *lhs, CsNode *rhs, int line) {
    CsNode *n = mk(CS_ASSIGN, line); n->assign.op = cs_strdup(op); n->assign.lhs = lhs; n->assign.rhs = rhs; return n;
}
CsNode *csnode_binary(const char *op, CsNode *l, CsNode *r, int line) {
    CsNode *n = mk(CS_BINARY, line); n->binary.op = cs_strdup(op); n->binary.left = l; n->binary.right = r; return n;
}
CsNode *csnode_unary(const char *op, CsNode *operand, int postfix, int line) {
    CsNode *n = mk(CS_UNARY, line); n->unary.op = cs_strdup(op); n->unary.operand = operand; n->unary.postfix = postfix; return n;
}
CsNode *csnode_ternary(CsNode *c, CsNode *t, CsNode *e, int line) {
    CsNode *n = mk(CS_TERNARY, line); n->ternary.cond = c; n->ternary.then_ = t; n->ternary.else_ = e; return n;
}
CsNode *csnode_call(CsNode *callee, CsNode **args, int argc, int line) {
    CsNode *n = mk(CS_CALL, line); n->call.callee = callee; n->call.args = args; n->call.argc = argc; return n;
}
CsNode *csnode_member(CsNode *obj, const char *name, int line) {
    CsNode *n = mk(CS_MEMBER, line); n->member.obj = obj; n->member.name = cs_strdup(name); return n;
}
CsNode *csnode_index(CsNode *obj, CsNode *index, int line) {
    CsNode *n = mk(CS_INDEX, line); n->index_.obj = obj; n->index_.index = index; return n;
}
CsNode *csnode_new_object(CsType *type, CsNode **args, int argc, int line) {
    CsNode *n = mk(CS_NEW_OBJECT, line); n->new_object.type = type; n->new_object.args = args; n->new_object.argc = argc; return n;
}
CsNode *csnode_new_array(CsType *elem_type, CsNode **elems, int n_elems, CsNode *size_expr, int line) {
    CsNode *n = mk(CS_NEW_ARRAY, line);
    n->new_array.elem_type = elem_type; n->new_array.elems = elems; n->new_array.n_elems = n_elems;
    n->new_array.size_expr = size_expr;
    return n;
}
CsNode *csnode_lambda(char **param_names, int n_params, CsNode *body, int body_is_expr, int line) {
    CsNode *n = mk(CS_LAMBDA, line);
    n->lambda.param_names = param_names; n->lambda.n_params = n_params;
    n->lambda.body = body; n->lambda.body_is_expr = body_is_expr;
    return n;
}
CsNode *csnode_cast(CsType *type, CsNode *expr, int line) {
    CsNode *n = mk(CS_CAST, line); n->cast.type = type; n->cast.expr = expr; return n;
}
CsNode *csnode_block(CsNode **stmts, int n_stmts, int line) {
    CsNode *n = mk(CS_BLOCK, line); n->block.stmts = stmts; n->block.n_stmts = n_stmts; return n;
}
CsNode *csnode_if(CsNode *cond, CsNode *then_, CsNode *else_, int line) {
    CsNode *n = mk(CS_IF, line); n->if_.cond = cond; n->if_.then_ = then_; n->if_.else_ = else_; return n;
}
CsNode *csnode_for(CsNode *init, CsNode *cond, CsNode *step, CsNode *body, int line) {
    CsNode *n = mk(CS_FOR, line); n->for_.init = init; n->for_.cond = cond; n->for_.step = step; n->for_.body = body; return n;
}
CsNode *csnode_foreach(CsType *elem_type, const char *var_name, CsNode *collection, CsNode *body, int line) {
    CsNode *n = mk(CS_FOREACH, line);
    n->foreach_.elem_type = elem_type; n->foreach_.var_name = cs_strdup(var_name);
    n->foreach_.collection = collection; n->foreach_.body = body;
    return n;
}
CsNode *csnode_while(CsNode *cond, CsNode *body, int line) {
    CsNode *n = mk(CS_WHILE, line); n->while_.cond = cond; n->while_.body = body; return n;
}
CsNode *csnode_do_while(CsNode *body, CsNode *cond, int line) {
    CsNode *n = mk(CS_DO_WHILE, line); n->do_while.body = body; n->do_while.cond = cond; return n;
}
CsNode *csnode_switch(CsNode *expr, CsNode **cases, int n_cases, int line) {
    CsNode *n = mk(CS_SWITCH, line); n->switch_.expr = expr; n->switch_.cases = cases; n->switch_.n_cases = n_cases; return n;
}
CsNode *csnode_switch_case(CsNode **values, int n_values, int is_default, CsNode **stmts, int n_stmts, int line) {
    CsNode *n = mk(CS_SWITCH_CASE, line);
    n->switch_case.values = values; n->switch_case.n_values = n_values; n->switch_case.is_default = is_default;
    n->switch_case.stmts = stmts; n->switch_case.n_stmts = n_stmts;
    return n;
}
CsNode *csnode_break(int line) { return mk(CS_BREAK, line); }
CsNode *csnode_continue(int line) { return mk(CS_CONTINUE, line); }
CsNode *csnode_return(CsNode *expr, int line) { CsNode *n = mk(CS_RETURN, line); n->return_.expr = expr; return n; }
CsNode *csnode_try(CsNode *try_block, CsNode **catches, int n_catches, CsNode *finally_block, int line) {
    CsNode *n = mk(CS_TRY, line);
    n->try_.try_block = try_block; n->try_.catches = catches; n->try_.n_catches = n_catches;
    n->try_.finally_block = finally_block;
    return n;
}
CsNode *csnode_catch_clause(CsType *ex_type, const char *var_name, CsNode *body, int line) {
    CsNode *n = mk(CS_CATCH_CLAUSE, line);
    n->catch_clause.ex_type = ex_type; n->catch_clause.var_name = var_name ? cs_strdup(var_name) : 0;
    n->catch_clause.body = body;
    return n;
}
CsNode *csnode_throw(CsNode *expr, int line) { CsNode *n = mk(CS_THROW, line); n->throw_.expr = expr; return n; }
CsNode *csnode_local_var_decl(CsType *type, const char *name, CsNode *init, int is_var, int line) {
    CsNode *n = mk(CS_LOCAL_VAR_DECL, line);
    n->local_var_decl.type = type; n->local_var_decl.name = cs_strdup(name);
    n->local_var_decl.init = init; n->local_var_decl.is_var = is_var;
    return n;
}
CsNode *csnode_expr_stmt(CsNode *expr, int line) { CsNode *n = mk(CS_EXPR_STMT, line); n->expr_stmt.expr = expr; return n; }
CsNode *csnode_unit(CsNode **usings, int n_usings, CsNode **decls, int n_decls, int line) {
    CsNode *n = mk(CS_UNIT, line);
    n->unit.usings = usings; n->unit.n_usings = n_usings; n->unit.decls = decls; n->unit.n_decls = n_decls;
    return n;
}
CsNode *csnode_using(const char *ns, int line) { CsNode *n = mk(CS_USING, line); n->using_.namespace_name = cs_strdup(ns); return n; }
CsNode *csnode_namespace(const char *name, CsNode **decls, int n_decls, int line) {
    CsNode *n = mk(CS_NAMESPACE, line);
    n->namespace_decl.name = cs_strdup(name);
    n->namespace_decl.decls = decls; n->namespace_decl.n_decls = n_decls;
    return n;
}
CsNode *csnode_class_decl(CsAstKind kind, const char *name, const char *base_class_name,
                           char **interface_names, int n_interfaces,
                           CsNode **type_params, int n_type_params,
                           CsNode **members, int n_members, int is_static, int line) {
    CsNode *n = mk(kind, line);
    n->class_decl.name = cs_strdup(name);
    n->class_decl.base_class_name = base_class_name ? cs_strdup(base_class_name) : 0;
    n->class_decl.interface_names = interface_names; n->class_decl.n_interfaces = n_interfaces;
    n->class_decl.type_params = type_params; n->class_decl.n_type_params = n_type_params;
    n->class_decl.members = members; n->class_decl.n_members = n_members;
    n->class_decl.is_static = is_static;
    return n;
}
CsNode *csnode_field_decl(CsType *type, const char *name, CsNode *init, int is_static, int line) {
    CsNode *n = mk(CS_FIELD_DECL, line);
    n->field_decl.type = type; n->field_decl.name = cs_strdup(name); n->field_decl.init = init; n->field_decl.is_static = is_static;
    return n;
}
CsNode *csnode_property_decl(CsType *type, const char *name, int is_static, int has_setter, int line) {
    CsNode *n = mk(CS_PROPERTY_DECL, line);
    n->property_decl.type = type; n->property_decl.name = cs_strdup(name);
    n->property_decl.is_static = is_static; n->property_decl.has_setter = has_setter;
    return n;
}
CsNode *csnode_method_decl(CsType *ret_type, const char *name,
                            CsNode **type_params, int n_type_params,
                            CsNode **params, int n_params, CsNode *body, int is_static, int line) {
    CsNode *n = mk(CS_METHOD_DECL, line);
    n->method_decl.ret_type = ret_type; n->method_decl.name = cs_strdup(name);
    n->method_decl.type_params = type_params; n->method_decl.n_type_params = n_type_params;
    n->method_decl.params = params; n->method_decl.n_params = n_params;
    n->method_decl.body = body; n->method_decl.is_static = is_static;
    return n;
}
CsNode *csnode_ctor_decl(const char *name, CsNode **params, int n_params, CsNode *body, int line) {
    CsNode *n = mk(CS_CTOR_DECL, line);
    n->ctor_decl.name = cs_strdup(name); n->ctor_decl.params = params; n->ctor_decl.n_params = n_params; n->ctor_decl.body = body;
    return n;
}
CsNode *csnode_param(CsType *type, const char *name, CsNode *default_value, int line) {
    CsNode *n = mk(CS_PARAM, line);
    n->param.type = type; n->param.name = cs_strdup(name); n->param.default_value = default_value;
    return n;
}
CsNode *csnode_type_param(const char *name, CsConstraint *constraints, int n_constraints, int line) {
    CsNode *n = mk(CS_TYPE_PARAM, line);
    n->type_param.name = cs_strdup(name); n->type_param.constraints = constraints; n->type_param.n_constraints = n_constraints;
    return n;
}

/* ---- print (debug) ---- */
static void ind(int n) { int i; for (i = 0; i < n; i++) printf("  "); }

void csast_print(const CsNode *n, int indent) {
    int i;
    if (!n) { ind(indent); printf("(null)\n"); return; }
    switch (n->kind) {
    case CS_LIT_INT: ind(indent); printf("Int(%lld)\n", n->lit_int.value); break;
    case CS_LIT_DOUBLE: ind(indent); printf("Double(%g)\n", n->lit_double.value); break;
    case CS_LIT_STRING: ind(indent); printf("String(\"%s\")\n", n->lit_string.value); break;
    case CS_LIT_INTERP_STRING:
        ind(indent); printf("InterpString(%d parts)\n", n->lit_interp.n_parts);
        for (i = 0; i < n->lit_interp.n_parts; i++) {
            if (n->lit_interp.parts[i].is_expr) csast_print(n->lit_interp.parts[i].expr, indent + 1);
            else { ind(indent + 1); printf("Text(\"%s\")\n", n->lit_interp.parts[i].text); }
        }
        break;
    case CS_LIT_BOOL: ind(indent); printf("Bool(%d)\n", n->lit_bool.value); break;
    case CS_LIT_NULL: ind(indent); printf("Null\n"); break;
    case CS_LIT_CHAR: ind(indent); printf("Char(%lld)\n", n->lit_char.value); break;
    case CS_IDENT: ind(indent); printf("Ident(%s)\n", n->ident.name); break;
    case CS_THIS: ind(indent); printf("This\n"); break;
    case CS_BASE: ind(indent); printf("Base\n"); break;
    case CS_ASSIGN:
        ind(indent); printf("Assign(%s)\n", n->assign.op);
        csast_print(n->assign.lhs, indent + 1); csast_print(n->assign.rhs, indent + 1);
        break;
    case CS_BINARY:
        ind(indent); printf("Binary(%s)\n", n->binary.op);
        csast_print(n->binary.left, indent + 1); csast_print(n->binary.right, indent + 1);
        break;
    case CS_UNARY:
        ind(indent); printf("Unary(%s,post=%d)\n", n->unary.op, n->unary.postfix);
        csast_print(n->unary.operand, indent + 1);
        break;
    case CS_TERNARY:
        ind(indent); printf("Ternary\n");
        csast_print(n->ternary.cond, indent + 1); csast_print(n->ternary.then_, indent + 1); csast_print(n->ternary.else_, indent + 1);
        break;
    case CS_CALL:
        ind(indent); printf("Call(argc=%d)\n", n->call.argc);
        csast_print(n->call.callee, indent + 1);
        for (i = 0; i < n->call.argc; i++) csast_print(n->call.args[i], indent + 1);
        break;
    case CS_MEMBER:
        ind(indent); printf("Member(.%s)\n", n->member.name);
        csast_print(n->member.obj, indent + 1);
        break;
    case CS_INDEX:
        ind(indent); printf("Index\n");
        csast_print(n->index_.obj, indent + 1); csast_print(n->index_.index, indent + 1);
        break;
    case CS_NEW_OBJECT: {
        char *ts = cstype_str(n->new_object.type);
        ind(indent); printf("NewObject(%s, argc=%d)\n", ts, n->new_object.argc);
        free(ts);
        for (i = 0; i < n->new_object.argc; i++) csast_print(n->new_object.args[i], indent + 1);
        break;
    }
    case CS_NEW_ARRAY: {
        char *ts = cstype_str(n->new_array.elem_type);
        ind(indent); printf("NewArray(%s[], n_elems=%d)\n", ts, n->new_array.n_elems);
        free(ts);
        for (i = 0; i < n->new_array.n_elems; i++) csast_print(n->new_array.elems[i], indent + 1);
        break;
    }
    case CS_LAMBDA:
        ind(indent); printf("Lambda(params=");
        for (i = 0; i < n->lambda.n_params; i++) printf("%s%s", i ? "," : "", n->lambda.param_names[i]);
        printf(", expr_body=%d)\n", n->lambda.body_is_expr);
        csast_print(n->lambda.body, indent + 1);
        break;
    case CS_CAST: {
        char *ts = cstype_str(n->cast.type);
        ind(indent); printf("Cast(%s)\n", ts); free(ts);
        csast_print(n->cast.expr, indent + 1);
        break;
    }
    case CS_BLOCK:
        ind(indent); printf("Block(%d stmts)\n", n->block.n_stmts);
        for (i = 0; i < n->block.n_stmts; i++) csast_print(n->block.stmts[i], indent + 1);
        break;
    case CS_IF:
        ind(indent); printf("If\n");
        csast_print(n->if_.cond, indent + 1); csast_print(n->if_.then_, indent + 1);
        if (n->if_.else_) csast_print(n->if_.else_, indent + 1);
        break;
    case CS_FOR:
        ind(indent); printf("For\n");
        if (n->for_.init) csast_print(n->for_.init, indent + 1);
        if (n->for_.cond) csast_print(n->for_.cond, indent + 1);
        if (n->for_.step) csast_print(n->for_.step, indent + 1);
        csast_print(n->for_.body, indent + 1);
        break;
    case CS_FOREACH: {
        char *ts = cstype_str(n->foreach_.elem_type);
        ind(indent); printf("Foreach(%s %s)\n", ts, n->foreach_.var_name); free(ts);
        csast_print(n->foreach_.collection, indent + 1); csast_print(n->foreach_.body, indent + 1);
        break;
    }
    case CS_WHILE:
        ind(indent); printf("While\n");
        csast_print(n->while_.cond, indent + 1); csast_print(n->while_.body, indent + 1);
        break;
    case CS_DO_WHILE:
        ind(indent); printf("DoWhile\n");
        csast_print(n->do_while.body, indent + 1); csast_print(n->do_while.cond, indent + 1);
        break;
    case CS_SWITCH:
        ind(indent); printf("Switch\n");
        csast_print(n->switch_.expr, indent + 1);
        for (i = 0; i < n->switch_.n_cases; i++) csast_print(n->switch_.cases[i], indent + 1);
        break;
    case CS_SWITCH_CASE: {
        int j;
        ind(indent); printf("Case(default=%d, n_values=%d)\n", n->switch_case.is_default, n->switch_case.n_values);
        for (j = 0; j < n->switch_case.n_values; j++) csast_print(n->switch_case.values[j], indent + 1);
        for (j = 0; j < n->switch_case.n_stmts; j++) csast_print(n->switch_case.stmts[j], indent + 1);
        break;
    }
    case CS_BREAK: ind(indent); printf("Break\n"); break;
    case CS_CONTINUE: ind(indent); printf("Continue\n"); break;
    case CS_RETURN:
        ind(indent); printf("Return\n");
        if (n->return_.expr) csast_print(n->return_.expr, indent + 1);
        break;
    case CS_TRY:
        ind(indent); printf("Try\n");
        csast_print(n->try_.try_block, indent + 1);
        for (i = 0; i < n->try_.n_catches; i++) csast_print(n->try_.catches[i], indent + 1);
        if (n->try_.finally_block) { ind(indent + 1); printf("Finally\n"); csast_print(n->try_.finally_block, indent + 2); }
        break;
    case CS_CATCH_CLAUSE: {
        char *ts = n->catch_clause.ex_type ? cstype_str(n->catch_clause.ex_type) : cs_strdup("<any>");
        ind(indent); printf("Catch(%s %s)\n", ts, n->catch_clause.var_name ? n->catch_clause.var_name : "");
        free(ts);
        csast_print(n->catch_clause.body, indent + 1);
        break;
    }
    case CS_THROW:
        ind(indent); printf("Throw\n");
        if (n->throw_.expr) csast_print(n->throw_.expr, indent + 1);
        break;
    case CS_LOCAL_VAR_DECL: {
        char *ts = n->local_var_decl.is_var ? cs_strdup("var") : cstype_str(n->local_var_decl.type);
        ind(indent); printf("LocalVarDecl(%s %s)\n", ts, n->local_var_decl.name); free(ts);
        if (n->local_var_decl.init) csast_print(n->local_var_decl.init, indent + 1);
        break;
    }
    case CS_EXPR_STMT:
        ind(indent); printf("ExprStmt\n");
        csast_print(n->expr_stmt.expr, indent + 1);
        break;
    case CS_UNIT:
        ind(indent); printf("Unit(usings=%d, decls=%d)\n", n->unit.n_usings, n->unit.n_decls);
        for (i = 0; i < n->unit.n_decls; i++) csast_print(n->unit.decls[i], indent + 1);
        break;
    case CS_USING: ind(indent); printf("Using(%s)\n", n->using_.namespace_name); break;
    case CS_NAMESPACE:
        ind(indent); printf("Namespace(%s)\n", n->namespace_decl.name);
        for (i = 0; i < n->namespace_decl.n_decls; i++) csast_print(n->namespace_decl.decls[i], indent + 1);
        break;
    case CS_CLASS_DECL: case CS_STRUCT_DECL: case CS_INTERFACE_DECL:
        ind(indent); printf("%s(%s%s%s, type_params=%d, members=%d)\n",
            n->kind == CS_CLASS_DECL ? "Class" : n->kind == CS_STRUCT_DECL ? "Struct" : "Interface",
            n->class_decl.name,
            n->class_decl.base_class_name ? " : " : "",
            n->class_decl.base_class_name ? n->class_decl.base_class_name : "",
            n->class_decl.n_type_params, n->class_decl.n_members);
        for (i = 0; i < n->class_decl.n_type_params; i++) csast_print(n->class_decl.type_params[i], indent + 1);
        for (i = 0; i < n->class_decl.n_members; i++) csast_print(n->class_decl.members[i], indent + 1);
        break;
    case CS_FIELD_DECL: {
        char *ts = cstype_str(n->field_decl.type);
        ind(indent); printf("Field(%s %s%s)\n", ts, n->field_decl.name, n->field_decl.is_static ? " [static]" : "");
        free(ts);
        if (n->field_decl.init) csast_print(n->field_decl.init, indent + 1);
        break;
    }
    case CS_PROPERTY_DECL: {
        char *ts = cstype_str(n->property_decl.type);
        ind(indent); printf("Property(%s %s%s%s)\n", ts, n->property_decl.name,
            n->property_decl.is_static ? " [static]" : "", n->property_decl.has_setter ? " {get;set;}" : " {get;}");
        free(ts);
        break;
    }
    case CS_METHOD_DECL: {
        char *ts = cstype_str(n->method_decl.ret_type);
        ind(indent); printf("Method(%s %s%s, type_params=%d, params=%d)\n", ts, n->method_decl.name,
            n->method_decl.is_static ? " [static]" : "", n->method_decl.n_type_params, n->method_decl.n_params);
        free(ts);
        for (i = 0; i < n->method_decl.n_type_params; i++) csast_print(n->method_decl.type_params[i], indent + 1);
        for (i = 0; i < n->method_decl.n_params; i++) csast_print(n->method_decl.params[i], indent + 1);
        if (n->method_decl.body) csast_print(n->method_decl.body, indent + 1);
        break;
    }
    case CS_CTOR_DECL:
        ind(indent); printf("Ctor(%s, params=%d)\n", n->ctor_decl.name, n->ctor_decl.n_params);
        for (i = 0; i < n->ctor_decl.n_params; i++) csast_print(n->ctor_decl.params[i], indent + 1);
        csast_print(n->ctor_decl.body, indent + 1);
        break;
    case CS_PARAM: {
        char *ts = cstype_str(n->param.type);
        ind(indent); printf("Param(%s %s)\n", ts, n->param.name); free(ts);
        break;
    }
    case CS_TYPE_PARAM:
        ind(indent); printf("TypeParam(%s, constraints=%d)\n", n->type_param.name, n->type_param.n_constraints);
        break;
    default:
        ind(indent); printf("<unknown kind %d>\n", (int)n->kind);
        break;
    }
}

/* ---- free ---- */
void csast_free(CsNode *n) {
    int i;
    if (!n) return;
    switch (n->kind) {
    case CS_LIT_STRING: free(n->lit_string.value); break;
    case CS_LIT_INTERP_STRING:
        for (i = 0; i < n->lit_interp.n_parts; i++) { free(n->lit_interp.parts[i].text); csast_free(n->lit_interp.parts[i].expr); }
        free(n->lit_interp.parts);
        break;
    case CS_IDENT: free(n->ident.name); break;
    case CS_ASSIGN: free(n->assign.op); csast_free(n->assign.lhs); csast_free(n->assign.rhs); break;
    case CS_BINARY: free(n->binary.op); csast_free(n->binary.left); csast_free(n->binary.right); break;
    case CS_UNARY: free(n->unary.op); csast_free(n->unary.operand); break;
    case CS_TERNARY: csast_free(n->ternary.cond); csast_free(n->ternary.then_); csast_free(n->ternary.else_); break;
    case CS_CALL:
        csast_free(n->call.callee);
        for (i = 0; i < n->call.argc; i++) csast_free(n->call.args[i]);
        free(n->call.args);
        break;
    case CS_MEMBER: csast_free(n->member.obj); free(n->member.name); break;
    case CS_INDEX: csast_free(n->index_.obj); csast_free(n->index_.index); break;
    case CS_NEW_OBJECT:
        cstype_free(n->new_object.type);
        for (i = 0; i < n->new_object.argc; i++) csast_free(n->new_object.args[i]);
        free(n->new_object.args);
        break;
    case CS_NEW_ARRAY:
        cstype_free(n->new_array.elem_type);
        for (i = 0; i < n->new_array.n_elems; i++) csast_free(n->new_array.elems[i]);
        free(n->new_array.elems);
        csast_free(n->new_array.size_expr);
        break;
    case CS_LAMBDA:
        for (i = 0; i < n->lambda.n_params; i++) free(n->lambda.param_names[i]);
        free(n->lambda.param_names);
        csast_free(n->lambda.body);
        break;
    case CS_CAST: cstype_free(n->cast.type); csast_free(n->cast.expr); break;
    case CS_BLOCK:
        for (i = 0; i < n->block.n_stmts; i++) csast_free(n->block.stmts[i]);
        free(n->block.stmts);
        break;
    case CS_IF: csast_free(n->if_.cond); csast_free(n->if_.then_); csast_free(n->if_.else_); break;
    case CS_FOR: csast_free(n->for_.init); csast_free(n->for_.cond); csast_free(n->for_.step); csast_free(n->for_.body); break;
    case CS_FOREACH:
        cstype_free(n->foreach_.elem_type); free(n->foreach_.var_name);
        csast_free(n->foreach_.collection); csast_free(n->foreach_.body);
        break;
    case CS_WHILE: csast_free(n->while_.cond); csast_free(n->while_.body); break;
    case CS_DO_WHILE: csast_free(n->do_while.body); csast_free(n->do_while.cond); break;
    case CS_SWITCH:
        csast_free(n->switch_.expr);
        for (i = 0; i < n->switch_.n_cases; i++) csast_free(n->switch_.cases[i]);
        free(n->switch_.cases);
        break;
    case CS_SWITCH_CASE: {
        int j;
        for (j = 0; j < n->switch_case.n_values; j++) csast_free(n->switch_case.values[j]);
        free(n->switch_case.values);
        for (j = 0; j < n->switch_case.n_stmts; j++) csast_free(n->switch_case.stmts[j]);
        free(n->switch_case.stmts);
        break;
    }
    case CS_RETURN: csast_free(n->return_.expr); break;
    case CS_TRY:
        csast_free(n->try_.try_block);
        for (i = 0; i < n->try_.n_catches; i++) csast_free(n->try_.catches[i]);
        free(n->try_.catches);
        csast_free(n->try_.finally_block);
        break;
    case CS_CATCH_CLAUSE:
        cstype_free(n->catch_clause.ex_type); free(n->catch_clause.var_name); csast_free(n->catch_clause.body);
        break;
    case CS_THROW: csast_free(n->throw_.expr); break;
    case CS_LOCAL_VAR_DECL:
        cstype_free(n->local_var_decl.type); free(n->local_var_decl.name); csast_free(n->local_var_decl.init);
        break;
    case CS_EXPR_STMT: csast_free(n->expr_stmt.expr); break;
    case CS_UNIT:
        for (i = 0; i < n->unit.n_usings; i++) csast_free(n->unit.usings[i]);
        free(n->unit.usings);
        for (i = 0; i < n->unit.n_decls; i++) csast_free(n->unit.decls[i]);
        free(n->unit.decls);
        break;
    case CS_USING: free(n->using_.namespace_name); break;
    case CS_NAMESPACE:
        free(n->namespace_decl.name);
        for (i = 0; i < n->namespace_decl.n_decls; i++) csast_free(n->namespace_decl.decls[i]);
        free(n->namespace_decl.decls);
        break;
    case CS_CLASS_DECL: case CS_STRUCT_DECL: case CS_INTERFACE_DECL:
        free(n->class_decl.name); free(n->class_decl.base_class_name);
        for (i = 0; i < n->class_decl.n_interfaces; i++) free(n->class_decl.interface_names[i]);
        free(n->class_decl.interface_names);
        for (i = 0; i < n->class_decl.n_type_params; i++) csast_free(n->class_decl.type_params[i]);
        free(n->class_decl.type_params);
        for (i = 0; i < n->class_decl.n_members; i++) csast_free(n->class_decl.members[i]);
        free(n->class_decl.members);
        break;
    case CS_FIELD_DECL: cstype_free(n->field_decl.type); free(n->field_decl.name); csast_free(n->field_decl.init); break;
    case CS_PROPERTY_DECL: cstype_free(n->property_decl.type); free(n->property_decl.name); break;
    case CS_METHOD_DECL:
        cstype_free(n->method_decl.ret_type); free(n->method_decl.name);
        for (i = 0; i < n->method_decl.n_type_params; i++) csast_free(n->method_decl.type_params[i]);
        free(n->method_decl.type_params);
        for (i = 0; i < n->method_decl.n_params; i++) csast_free(n->method_decl.params[i]);
        free(n->method_decl.params);
        csast_free(n->method_decl.body);
        break;
    case CS_CTOR_DECL:
        free(n->ctor_decl.name);
        for (i = 0; i < n->ctor_decl.n_params; i++) csast_free(n->ctor_decl.params[i]);
        free(n->ctor_decl.params);
        for (i = 0; i < n->ctor_decl.n_base_args; i++) csast_free(n->ctor_decl.base_args[i]);
        free(n->ctor_decl.base_args);
        csast_free(n->ctor_decl.body);
        break;
    case CS_PARAM: cstype_free(n->param.type); free(n->param.name); csast_free(n->param.default_value); break;
    case CS_TYPE_PARAM:
        free(n->type_param.name);
        for (i = 0; i < n->type_param.n_constraints; i++) free(n->type_param.constraints[i].base_type_name);
        free(n->type_param.constraints);
        break;
    default: break;
    }
    free(n);
}
