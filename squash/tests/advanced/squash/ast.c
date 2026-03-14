#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strdup _strdup
#endif

static ASTNode *alloc_node(ASTKind kind, int line) {
    ASTNode *n = calloc(1, sizeof(ASTNode));
    if (!n) { perror("calloc"); exit(1); }
    n->kind = kind;
    n->line = line;
    return n;
}

ASTNode *ast_number(long long value, int line) {
    ASTNode *n = alloc_node(AST_NUMBER, line);
    n->num.value = value;
    return n;
}
ASTNode *ast_string(const char *value, int line) {
    ASTNode *n = alloc_node(AST_STRING, line);
    n->str.value = strdup(value);
    return n;
}
ASTNode *ast_var(const char *name, int line) {
    ASTNode *n = alloc_node(AST_VAR, line);
    n->var.name = strdup(name);
    return n;
}
ASTNode *ast_unary(char op, ASTNode *operand, int line) {
    ASTNode *n = alloc_node(AST_UNARY, line);
    n->unary.op = op;
    n->unary.operand = operand;
    return n;
}
ASTNode *ast_binary(const char *op, ASTNode *left, ASTNode *right, int line) {
    ASTNode *n = alloc_node(AST_BINARY, line);
    strncpy(n->binary.op, op, sizeof n->binary.op - 1);
    n->binary.left  = left;
    n->binary.right = right;
    return n;
}
ASTNode *ast_assign(ASTNode *lhs, ASTNode *rhs, int line) {
    ASTNode *n = alloc_node(AST_ASSIGN, line);
    n->assign.lhs = lhs;
    n->assign.rhs = rhs;
    return n;
}
ASTNode *ast_call(const char *name, ASTNode **args, int argc, int line) {
    ASTNode *n = alloc_node(AST_CALL, line);
    n->call.name = strdup(name);
    n->call.argc = argc;
    if (argc > 0) {
        n->call.args = malloc(argc * sizeof(ASTNode *));
        memcpy(n->call.args, args, argc * sizeof(ASTNode *));
    }
    return n;
}
ASTNode *ast_if(ASTNode *cond, ASTNode *then_, ASTNode *else_, int line) {
    ASTNode *n = alloc_node(AST_IF, line);
    n->if_.cond  = cond;
    n->if_.then_ = then_;
    n->if_.else_ = else_;
    return n;
}
ASTNode *ast_while(ASTNode *cond, ASTNode *body, int line) {
    ASTNode *n = alloc_node(AST_WHILE, line);
    n->while_.cond = cond;
    n->while_.body = body;
    return n;
}
ASTNode *ast_return(ASTNode *expr, int line) {
    ASTNode *n = alloc_node(AST_RETURN, line);
    n->ret.expr = expr;
    return n;
}
ASTNode *ast_expr_stmt(ASTNode *expr, int line) {
    ASTNode *n = alloc_node(AST_EXPR_STMT, line);
    n->expr_stmt.expr = expr;
    return n;
}
ASTNode *ast_var_decl(const char *type, const char *name, ASTNode *init, int line) {
    ASTNode *n = alloc_node(AST_VAR_DECL, line);
    n->var_decl.type_name = strdup(type);
    n->var_decl.name      = strdup(name);
    n->var_decl.init      = init;
    return n;
}
ASTNode *ast_block(ASTNode **stmts, int count, int line) {
    ASTNode *n = alloc_node(AST_BLOCK, line);
    n->block.count = count;
    if (count > 0) {
        n->block.stmts = malloc(count * sizeof(ASTNode *));
        memcpy(n->block.stmts, stmts, count * sizeof(ASTNode *));
    }
    return n;
}
ASTNode *ast_func_decl(const char *ret, const char *name,
                        ASTNode **params, int paramc,
                        ASTNode *body, int line) {
    ASTNode *n = alloc_node(AST_FUNC_DECL, line);
    n->func.ret_type = strdup(ret);
    n->func.name     = strdup(name);
    n->func.paramc   = paramc;
    n->func.body     = body;
    if (paramc > 0) {
        n->func.params = malloc(paramc * sizeof(ASTNode *));
        memcpy(n->func.params, params, paramc * sizeof(ASTNode *));
    }
    return n;
}
ASTNode *ast_param(const char *type, const char *name, int line) {
    ASTNode *n = alloc_node(AST_PARAM, line);
    n->param.type_name = strdup(type);
    n->param.name      = strdup(name);
    return n;
}
ASTNode *ast_program(ASTNode **decls, int count, int line) {
    ASTNode *n = alloc_node(AST_PROGRAM, line);
    n->program.count = count;
    if (count > 0) {
        n->program.decls = malloc(count * sizeof(ASTNode *));
        memcpy(n->program.decls, decls, count * sizeof(ASTNode *));
    }
    return n;
}

/* =========================================================================
 * Pretty-printer
 * ========================================================================= */
static void indent_print(int d) {
    for (int i=0;i<d;i++) printf("  ");
}

void ast_print(const ASTNode *n, int depth) {
    if (!n) { indent_print(depth); printf("(null)\n"); return; }
    indent_print(depth);
    switch (n->kind) {
        case AST_NUMBER:
            printf("Number(%lld)\n", n->num.value); break;
        case AST_STRING:
            printf("String(\"%s\")\n", n->str.value); break;
        case AST_VAR:
            printf("Var(%s)\n", n->var.name); break;
        case AST_UNARY:
            printf("Unary(%c)\n", n->unary.op);
            ast_print(n->unary.operand, depth+1); break;
        case AST_BINARY:
            printf("Binary(%s)\n", n->binary.op);
            ast_print(n->binary.left,  depth+1);
            ast_print(n->binary.right, depth+1); break;
        case AST_ASSIGN:
            printf("Assign\n");
            ast_print(n->assign.lhs, depth+1);
            ast_print(n->assign.rhs, depth+1); break;
        case AST_CALL:
            printf("Call(%s, %d args)\n", n->call.name, n->call.argc);
            for (int i=0;i<n->call.argc;i++) ast_print(n->call.args[i],depth+1);
            break;
        case AST_IF:
            printf("If\n");
            indent_print(depth+1); printf("Cond:\n");
            ast_print(n->if_.cond,  depth+2);
            indent_print(depth+1); printf("Then:\n");
            ast_print(n->if_.then_, depth+2);
            if (n->if_.else_) {
                indent_print(depth+1); printf("Else:\n");
                ast_print(n->if_.else_, depth+2);
            }
            break;
        case AST_WHILE:
            printf("While\n");
            indent_print(depth+1); printf("Cond:\n");
            ast_print(n->while_.cond, depth+2);
            indent_print(depth+1); printf("Body:\n");
            ast_print(n->while_.body, depth+2); break;
        case AST_RETURN:
            printf("Return\n");
            if (n->ret.expr) ast_print(n->ret.expr, depth+1); break;
        case AST_EXPR_STMT:
            printf("ExprStmt\n");
            ast_print(n->expr_stmt.expr, depth+1); break;
        case AST_VAR_DECL:
            printf("VarDecl(%s %s)\n", n->var_decl.type_name, n->var_decl.name);
            if (n->var_decl.init) ast_print(n->var_decl.init, depth+1); break;
        case AST_BLOCK:
            printf("Block(%d stmts)\n", n->block.count);
            for (int i=0;i<n->block.count;i++) ast_print(n->block.stmts[i],depth+1);
            break;
        case AST_FUNC_DECL:
            printf("FuncDecl(%s %s, %d params)\n",
                   n->func.ret_type, n->func.name, n->func.paramc);
            for (int i=0;i<n->func.paramc;i++) ast_print(n->func.params[i],depth+1);
            ast_print(n->func.body, depth+1); break;
        case AST_PARAM:
            printf("Param(%s %s)\n", n->param.type_name, n->param.name); break;
        case AST_PROGRAM:
            printf("Program(%d decls)\n", n->program.count);
            for (int i=0;i<n->program.count;i++) ast_print(n->program.decls[i],depth+1);
            break;
        default:
            printf("Unknown AST node %d\n", n->kind);
    }
}

void ast_free(ASTNode *n) {
    if (!n) return;
    switch (n->kind) {
        case AST_STRING:   free(n->str.value); break;
        case AST_VAR:      free(n->var.name);  break;
        case AST_UNARY:    ast_free(n->unary.operand); break;
        case AST_BINARY:   ast_free(n->binary.left); ast_free(n->binary.right); break;
        case AST_ASSIGN:   ast_free(n->assign.lhs); ast_free(n->assign.rhs); break;
        case AST_CALL:
            free(n->call.name);
            for (int i=0;i<n->call.argc;i++) ast_free(n->call.args[i]);
            free(n->call.args); break;
        case AST_IF:
            ast_free(n->if_.cond); ast_free(n->if_.then_); ast_free(n->if_.else_); break;
        case AST_WHILE:
            ast_free(n->while_.cond); ast_free(n->while_.body); break;
        case AST_RETURN:   ast_free(n->ret.expr); break;
        case AST_EXPR_STMT: ast_free(n->expr_stmt.expr); break;
        case AST_VAR_DECL:
            free(n->var_decl.type_name); free(n->var_decl.name);
            ast_free(n->var_decl.init); break;
        case AST_BLOCK:
            for (int i=0;i<n->block.count;i++) ast_free(n->block.stmts[i]);
            free(n->block.stmts); break;
        case AST_FUNC_DECL:
            free(n->func.ret_type); free(n->func.name);
            for (int i=0;i<n->func.paramc;i++) ast_free(n->func.params[i]);
            free(n->func.params); ast_free(n->func.body); break;
        case AST_PARAM:
            free(n->param.type_name); free(n->param.name); break;
        case AST_PROGRAM:
            for (int i=0;i<n->program.count;i++) ast_free(n->program.decls[i]);
            free(n->program.decls); break;
        default: break;
    }
    free(n);
}
