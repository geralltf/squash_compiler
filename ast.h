#ifndef AST_H
#define AST_H
#include <stddef.h>

typedef enum {
    /* Literals */
    AST_NUMBER, AST_FLOAT, AST_STRING, AST_CHAR_LIT,
    /* Expressions */
    AST_VAR,            /* variable / identifier reference      */
    AST_CALL,           /* function call: name(args) or fptr(args) */
    AST_UNARY,          /* unary prefix/postfix                 */
    AST_BINARY,         /* binary operator                      */
    AST_ASSIGN,         /* lhs = rhs (and +=, -=, etc.)         */
    AST_TERNARY,        /* cond ? then : else                   */
    AST_CAST,           /* (type)expr                           */
    AST_SIZEOF_EXPR,    /* sizeof(expr)                         */
    AST_SIZEOF_TYPE,    /* sizeof(type)                         */
    AST_INDEX,          /* array[index]                         */
    AST_MEMBER,         /* expr.name  or  expr->name            */
    AST_ADDR,           /* &lvalue                              */
    AST_DEREF,          /* *ptr                                 */
    AST_FUNC_PTR_CALL,  /* (*fp)(args)                          */
    /* Statements */
    AST_BLOCK,
    AST_IF,
    AST_WHILE,
    AST_DO_WHILE,       /* do body while (cond)                 */
    AST_FOR,            /* for (init;cond;step) body            */
    AST_SWITCH,         /* switch(expr){case/default}           */
    AST_CASE,           /* case value:                          */
    AST_DEFAULT,        /* default:                             */
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_GOTO,           /* goto label                           */
    AST_LABEL,          /* label:                               */
    AST_EXPR_STMT,
    AST_VAR_DECL,       /* [storclass] type name[N] [= init]    */
    /* Top-level declarations */
    AST_FUNC_DECL,
    AST_PARAM,
    AST_STRUCT_DECL,    /* struct/union definition              */
    AST_FIELD,          /* struct field                         */
    AST_ENUM_DECL,      /* enum definition                      */
    AST_ENUM_VAL,       /* enum enumerator                      */
    AST_TYPEDEF_DECL,   /* typedef                              */
    AST_PROGRAM
} ASTKind;

/* Type representation */
typedef struct TypeInfo TypeInfo;
struct TypeInfo {
    char *base;         /* "int","char","void","struct Foo",etc  */
    int   pointer_depth;/* number of * levels                   */
    int   is_const;
    int   is_unsigned;
    int   array_size;   /* -1 = not an array, 0 = [] incomplete */
    TypeInfo *pointed_to; /* for pointer types                  */
    int   is_volatile;
    int   is_inline;
    int   is_extern;
    int   is_float;     /* float/double type                  */
};

typedef struct ASTNode ASTNode;
struct ASTNode {
    ASTKind  kind;
    int      line;
    TypeInfo *type;     /* set by parser / type checker          */

    union {
        struct { long long value; }                       num;
        struct { double value; }                          fnum;
        struct { char *value; }                           str;
        struct { long long value; }                       char_lit;
        struct { char *name; }                            var;
        struct { char op[4]; ASTNode *operand; int post; } unary; /* post=1 for postfix */
        struct { char op[4]; ASTNode *left; ASTNode *right; } binary;
        struct { char op[4]; ASTNode *lhs;  ASTNode *rhs;  } assign;
        struct { ASTNode *cond; ASTNode *then_; ASTNode *else_; } ternary;
        struct { TypeInfo *type; ASTNode *expr; }         cast;
        struct { ASTNode *expr; }                         sizeof_expr;
        struct { TypeInfo *type; }                        sizeof_type;
        struct { ASTNode *array; ASTNode *index; }        index;
        struct { ASTNode *obj; char *field; int arrow; }  member;
        struct { ASTNode *operand; }                      addr;
        struct { ASTNode *operand; }                      deref;
        struct {
            ASTNode  *func_expr;   /* expression yielding fn ptr */
            ASTNode **args; int argc;
        } fp_call;
        struct {
            char     *name;        /* function name for direct calls */
            ASTNode **args; int argc;
        } call;
        struct { ASTNode *cond; ASTNode *then_; ASTNode *else_; } if_;
        struct { ASTNode *cond; ASTNode *body; }          while_;
        struct { ASTNode *body; ASTNode *cond; }          do_while;
        struct { ASTNode *init; ASTNode *cond; ASTNode *step; ASTNode *body; } for_;
        struct { ASTNode *expr; ASTNode **cases; int nc; } switch_;
        struct { long long value; ASTNode **body; int nb; int is_default; } case_;
        struct { ASTNode **body; int nb; }                default_;
        struct { ASTNode *expr; }                         ret;
        struct { char *label; }                           goto_;
        struct { char *name; ASTNode *stmt; }             label;
        struct { ASTNode *expr; }                         expr_stmt;
        struct {
            char    *storage;   /* "auto","static","extern","register" */
            TypeInfo *type;
            char    *name;
            ASTNode *init;
            int      array_size; /* 0 = no array, -1 = [], >0 = [N] */
        } var_decl;
        struct {
            ASTNode **stmts; int count;
        } block;
        struct {
            char      *storage;
            TypeInfo  *ret_type;
            char      *name;
            ASTNode  **params; int paramc;
            ASTNode   *body;   /* NULL = declaration only */
            int        is_variadic;
        } func;
        struct {
            TypeInfo *type;
            char     *name;
            int       is_variadic;
        } param;
        struct {
            char      *name;   /* struct/union name (may be NULL for anon) */
            int        is_union;
            ASTNode  **fields; int nfields;
        } struct_decl;
        struct {
            TypeInfo *type;
            char     *name;
            int       array_size;
        } field;
        struct {
            char      *name;
            ASTNode  **vals; int nvals;
        } enum_decl;
        struct {
            char     *name;
            long long  value;
            int        has_value;
        } enum_val;
        struct {
            TypeInfo *type;
            char     *name;
        } typedef_decl;
        struct {
            ASTNode **decls; int count;
        } program;
    };
};

/* Constructors */
ASTNode *ast_number     (long long v, int line);
ASTNode *ast_float      (double v, int line);
ASTNode *ast_string     (const char *v, int line);
ASTNode *ast_char_lit   (long long v, int line);
ASTNode *ast_var        (const char *name, int line);
ASTNode *ast_unary      (const char *op, ASTNode *operand, int post, int line);
ASTNode *ast_binary     (const char *op, ASTNode *left, ASTNode *right, int line);
ASTNode *ast_assign     (const char *op, ASTNode *lhs, ASTNode *rhs, int line);
ASTNode *ast_ternary    (ASTNode *cond, ASTNode *then_, ASTNode *else_, int line);
ASTNode *ast_cast       (TypeInfo *type, ASTNode *expr, int line);
ASTNode *ast_sizeof_expr(ASTNode *expr, int line);
ASTNode *ast_sizeof_type(TypeInfo *type, int line);
ASTNode *ast_index      (ASTNode *array, ASTNode *index, int line);
ASTNode *ast_member     (ASTNode *obj, const char *field, int arrow, int line);
ASTNode *ast_addr       (ASTNode *operand, int line);
ASTNode *ast_deref      (ASTNode *operand, int line);
ASTNode *ast_fp_call    (ASTNode *func_expr, ASTNode **args, int argc, int line);
ASTNode *ast_call       (const char *name, ASTNode **args, int argc, int line);
ASTNode *ast_if         (ASTNode *cond, ASTNode *then_, ASTNode *else_, int line);
ASTNode *ast_while      (ASTNode *cond, ASTNode *body, int line);
ASTNode *ast_do_while   (ASTNode *body, ASTNode *cond, int line);
ASTNode *ast_for        (ASTNode *init, ASTNode *cond, ASTNode *step, ASTNode *body, int line);
ASTNode *ast_switch     (ASTNode *expr, ASTNode **cases, int nc, int line);
ASTNode *ast_case       (long long value, ASTNode **body, int nb, int line);
ASTNode *ast_default    (ASTNode **body, int nb, int line);
ASTNode *ast_return     (ASTNode *expr, int line);
ASTNode *ast_break      (int line);
ASTNode *ast_continue   (int line);
ASTNode *ast_goto       (const char *label, int line);
ASTNode *ast_label      (const char *name, ASTNode *stmt, int line);
ASTNode *ast_expr_stmt  (ASTNode *expr, int line);
ASTNode *ast_var_decl   (const char *storage, TypeInfo *type, const char *name,
                         ASTNode *init, int array_size, int line);
ASTNode *ast_block      (ASTNode **stmts, int count, int line);
ASTNode *ast_func_decl  (const char *storage, TypeInfo *ret_type, const char *name,
                         ASTNode **params, int paramc, int variadic,
                         ASTNode *body, int line);
ASTNode *ast_param      (TypeInfo *type, const char *name, int variadic, int line);
ASTNode *ast_struct_decl(const char *name, int is_union, ASTNode **fields, int nf, int line);
ASTNode *ast_field      (TypeInfo *type, const char *name, int array_size, int line);
ASTNode *ast_enum_decl  (const char *name, ASTNode **vals, int nv, int line);
ASTNode *ast_enum_val   (const char *name, long long value, int has_value, int line);
ASTNode *ast_typedef_decl(TypeInfo *type, const char *name, int line);
ASTNode *ast_program    (ASTNode **decls, int count, int line);

TypeInfo *typeinfo_new  (const char *base);
TypeInfo *typeinfo_ptr  (TypeInfo *base);
void      typeinfo_free (TypeInfo *t);
char     *typeinfo_str  (const TypeInfo *t);
int       typeinfo_size (const TypeInfo *t, int is_64bit);
int       typeinfo_is_float(const TypeInfo *t);
int       typeinfo_is_unsigned(const TypeInfo *t);

void ast_print(const ASTNode *n, int indent);
void ast_free (ASTNode *n);

#endif
