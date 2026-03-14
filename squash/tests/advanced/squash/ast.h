#ifndef AST_H
#define AST_H

#include <stddef.h>

/* =========================================================================
 * AST node kinds
 * ========================================================================= */
typedef enum {
    /* Literals */
    AST_NUMBER,         /* integer constant               */
    AST_STRING,         /* string literal                 */
    /* Variables / calls */
    AST_VAR,            /* variable reference             */
    AST_CALL,           /* function call                  */
    /* Operators */
    AST_UNARY,          /* unary prefix operator          */
    AST_BINARY,         /* binary operator                */
    AST_ASSIGN,         /* lhs = rhs                      */
    /* Statements */
    AST_BLOCK,          /* { stmt* }                      */
    AST_IF,             /* if (cond) then [else else_]    */
    AST_WHILE,          /* while (cond) body              */
    AST_RETURN,         /* return expr?                   */
    AST_EXPR_STMT,      /* expression used as statement   */
    AST_VAR_DECL,       /* type name [= init]             */
    /* Declarations */
    AST_FUNC_DECL,      /* function definition            */
    AST_PARAM,          /* function parameter             */
    AST_PROGRAM         /* top-level list of decls        */
} ASTKind;

/* =========================================================================
 * AST node
 * ========================================================================= */
typedef struct ASTNode ASTNode;
struct ASTNode {
    ASTKind  kind;
    int      line;

    union {
        /* AST_NUMBER */
        struct { long long value; } num;

        /* AST_STRING */
        struct { char *value; } str;

        /* AST_VAR */
        struct { char *name; } var;

        /* AST_UNARY */
        struct { char op; ASTNode *operand; } unary;

        /* AST_BINARY */
        struct { char op[3]; ASTNode *left; ASTNode *right; } binary;

        /* AST_ASSIGN */
        struct { ASTNode *lhs; ASTNode *rhs; } assign;

        /* AST_CALL */
        struct {
            char     *name;
            ASTNode **args;
            int       argc;
        } call;

        /* AST_IF */
        struct {
            ASTNode *cond;
            ASTNode *then_;
            ASTNode *else_;  /* NULL if no else branch */
        } if_;

        /* AST_WHILE */
        struct {
            ASTNode *cond;
            ASTNode *body;
        } while_;

        /* AST_RETURN */
        struct { ASTNode *expr; } ret;

        /* AST_EXPR_STMT */
        struct { ASTNode *expr; } expr_stmt;

        /* AST_VAR_DECL */
        struct {
            char    *type_name;
            char    *name;
            ASTNode *init;   /* NULL if no initialiser */
        } var_decl;

        /* AST_BLOCK */
        struct {
            ASTNode **stmts;
            int       count;
        } block;

        /* AST_FUNC_DECL */
        struct {
            char     *ret_type;
            char     *name;
            ASTNode **params;
            int       paramc;
            ASTNode  *body;   /* AST_BLOCK */
        } func;

        /* AST_PARAM */
        struct {
            char *type_name;
            char *name;
        } param;

        /* AST_PROGRAM */
        struct {
            ASTNode **decls;
            int       count;
        } program;
    };
};

/* =========================================================================
 * Constructors
 * ========================================================================= */
ASTNode *ast_number   (long long value, int line);
ASTNode *ast_string   (const char *value, int line);
ASTNode *ast_var      (const char *name, int line);
ASTNode *ast_unary    (char op, ASTNode *operand, int line);
ASTNode *ast_binary   (const char *op, ASTNode *left, ASTNode *right, int line);
ASTNode *ast_assign   (ASTNode *lhs, ASTNode *rhs, int line);
ASTNode *ast_call     (const char *name, ASTNode **args, int argc, int line);
ASTNode *ast_if       (ASTNode *cond, ASTNode *then_, ASTNode *else_, int line);
ASTNode *ast_while    (ASTNode *cond, ASTNode *body, int line);
ASTNode *ast_return   (ASTNode *expr, int line);
ASTNode *ast_expr_stmt(ASTNode *expr, int line);
ASTNode *ast_var_decl (const char *type, const char *name, ASTNode *init, int line);
ASTNode *ast_block    (ASTNode **stmts, int count, int line);
ASTNode *ast_func_decl(const char *ret, const char *name,
                        ASTNode **params, int paramc,
                        ASTNode *body, int line);
ASTNode *ast_param    (const char *type, const char *name, int line);
ASTNode *ast_program  (ASTNode **decls, int count, int line);

void ast_print(const ASTNode *n, int indent);
void ast_free (ASTNode *n);

#endif /* AST_H */
