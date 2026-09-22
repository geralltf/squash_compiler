#ifndef CPP_AST_H
#define CPP_AST_H

/* =========================================================================
 * cpp_ast — the C++ AST. Modeled stylistically on CS/cs_ast.h (a
 * deliberately separate node/type shape from squash's own ast.h/ASTNode),
 * but with C++'s own shape: pointers/references, namespaces via "::",
 * single inheritance + virtual dispatch, operator overloading, function/
 * method overloading, and templates (monomorphized by cpp_lower.c, which
 * turns this tree into plain C SOURCE TEXT fed through the SAME unmodified
 * C pipeline every .c file goes through — see cpp_lower.h for the exact,
 * honest scope of what a .cpp file can contain).
 * ========================================================================= */

typedef enum {
    CPP_LIT_INT, CPP_LIT_DOUBLE, CPP_LIT_STRING, CPP_LIT_CHAR, CPP_LIT_BOOL, CPP_LIT_NULLPTR,
    CPP_IDENT, CPP_THIS,
    CPP_ASSIGN, CPP_BINARY, CPP_UNARY, CPP_TERNARY,
    CPP_CALL, CPP_MEMBER, CPP_ARROW, CPP_INDEX, CPP_SCOPE,      /* SCOPE: "A::B" as an expression (qualified ident) */
    CPP_NEW, CPP_NEW_ARRAY, CPP_DELETE, CPP_DELETE_ARRAY, CPP_CAST, CPP_SIZEOF,
    CPP_BLOCK, CPP_IF, CPP_FOR, CPP_WHILE, CPP_DO_WHILE, CPP_SWITCH, CPP_SWITCH_CASE,
    CPP_BREAK, CPP_CONTINUE, CPP_RETURN, CPP_VAR_DECL, CPP_EXPR_STMT,
    CPP_UNIT, CPP_USING_NAMESPACE, CPP_USING_DECL, CPP_NAMESPACE,
    CPP_CLASS_DECL, CPP_FIELD_DECL, CPP_METHOD_DECL, CPP_CTOR_DECL, CPP_DTOR_DECL,
    CPP_FUNC_DECL, CPP_PARAM, CPP_TEMPLATE_PARAM,
    CPP_INCLUDE_RAW  /* an #include the lower pass just re-emits verbatim (for system/native headers) */
} CppAstKind;

typedef enum { CPP_ACC_PUBLIC, CPP_ACC_PROTECTED, CPP_ACC_PRIVATE } CppAccess;

/* Type reference (not a declaration) — "int", "Point", "std::string",
 * "T", "int*", "int&", "std::vector<int>". `ptr_depth` counts trailing
 * '*'; `is_ref` marks a trailing '&'; `type_args` holds template
 * arguments (empty for a non-template type). */
typedef struct CppType CppType;
struct CppType {
    char      *name;         /* possibly "::"-qualified, e.g. "std::string" */
    CppType  **type_args;  int n_type_args;
    int        ptr_depth;
    int        is_ref;
    int        is_const;
};

CppType *cpptype_new(const char *name);
CppType *cpptype_copy(const CppType *t);
void     cpptype_free(CppType *t);
char    *cpptype_mangle(const CppType *t);   /* stable suffix used in overload/template name-mangling */

typedef struct CppNode CppNode;

struct CppNode {
    CppAstKind kind;
    int        line;

    union {
        struct { long long value; }                        lit_int;
        struct { double value; }                            lit_double;
        struct { char *value; }                              lit_string;
        struct { long long value; }                          lit_char;
        struct { int value; }                                 lit_bool;
        struct { char *name; }                                 ident;

        struct { char *op; CppNode *lhs; CppNode *rhs; }      assign;
        struct { char *op; CppNode *left; CppNode *right; }   binary;
        struct { char *op; CppNode *operand; int postfix; }   unary;
        struct { CppNode *cond; CppNode *then_; CppNode *else_; } ternary;

        struct { CppNode *callee; CppNode **args; int argc; } call;
        struct { CppNode *obj; char *name; }                   member;
        struct { CppNode *obj; char *name; }                   arrow;
        struct { CppNode *obj; CppNode *index; }                index_;
        struct { char *qualifier; char *name; }                 scope;

        struct { CppType *type; CppNode **args; int argc; }    new_;
        struct { CppType *type; CppNode *size_expr; }           new_array;
        struct { CppNode *expr; }                                delete_;
        struct { CppNode *expr; }                                delete_array;
        struct { CppType *type; CppNode *expr; }                 cast;
        struct { CppType *type; CppNode *expr; }                 sizeof_; /* one of type/expr set */

        struct { CppNode **stmts; int n_stmts; }                 block;
        struct { CppNode *cond; CppNode *then_; CppNode *else_; } if_;
        struct { CppNode *init; CppNode *cond; CppNode *step; CppNode *body; } for_;
        struct { CppNode *cond; CppNode *body; }                 while_;
        struct { CppNode *body; CppNode *cond; }                 do_while;
        struct { CppNode *expr; CppNode **cases; int n_cases; }  switch_;
        struct { CppNode *value; CppNode **stmts; int n_stmts; } switch_case; /* value==NULL means "default" */
        struct { CppNode *expr; }                                 return_;
        struct { CppType *type; char *name; CppNode *init; }      var_decl;
        struct { CppNode *expr; }                                 expr_stmt;

        struct { CppNode **decls; int n_decls; }                  unit;
        struct { char *name; }                                     using_namespace;
        struct { char *qualified_name; }                            using_decl;
        struct { char *name; CppNode **decls; int n_decls; }        namespace_;

        struct {
            char      *name;
            int        is_struct;             /* default access: public if struct, private if class */
            char      *base_class_name;       /* single base, or NULL */
            CppNode  **template_params; int n_template_params;  /* CPP_TEMPLATE_PARAM */
            CppNode  **members;         int n_members;
        } class_decl;

        struct { CppType *type; char *name; CppNode *init; CppAccess access; int is_static; } field_decl;
        struct {
            CppType   *ret_type;
            char      *name;
            CppNode  **params;      int n_params;
            CppNode   *body;        /* NULL = declaration only */
            CppAccess  access;
            int        is_static;
            int        is_virtual;
            int        is_const;    /* trailing "const" qualifier, parsed + ignored for codegen purposes */
            char      *op_name;     /* non-NULL for "operator+" etc: canonical op text, e.g. "+", "==", "<<" */
        } method_decl;
        struct { char *name; CppNode **params; int n_params; CppNode *body; CppNode **init_list_args; int n_init_args; char *init_list_target; CppAccess access; } ctor_decl;
        struct { char *name; CppNode *body; CppAccess access; int is_virtual; } dtor_decl;

        struct {
            CppType   *ret_type;
            char      *name;
            CppNode  **template_params; int n_template_params;
            CppNode  **params;          int n_params;
            CppNode   *body;
        } func_decl;

        struct { CppType *type; char *name; CppNode *default_value; } param;
        struct { char *name; } template_param;   /* "typename T" / "class T" */

        struct { char *text; } include_raw;
    };
};

CppNode *cppnode_lit_int(long long v, int line);
CppNode *cppnode_lit_double(double v, int line);
CppNode *cppnode_lit_string(const char *v, int line);
CppNode *cppnode_lit_char(long long v, int line);
CppNode *cppnode_lit_bool(int v, int line);
CppNode *cppnode_lit_nullptr(int line);
CppNode *cppnode_ident(const char *name, int line);
CppNode *cppnode_this(int line);
CppNode *cppnode_assign(const char *op, CppNode *lhs, CppNode *rhs, int line);
CppNode *cppnode_binary(const char *op, CppNode *l, CppNode *r, int line);
CppNode *cppnode_unary(const char *op, CppNode *operand, int postfix, int line);
CppNode *cppnode_ternary(CppNode *c, CppNode *t, CppNode *e, int line);
CppNode *cppnode_call(CppNode *callee, CppNode **args, int argc, int line);
CppNode *cppnode_member(CppNode *obj, const char *name, int line);
CppNode *cppnode_arrow(CppNode *obj, const char *name, int line);
CppNode *cppnode_index(CppNode *obj, CppNode *index, int line);
CppNode *cppnode_scope(const char *qualifier, const char *name, int line);
CppNode *cppnode_new(CppType *type, CppNode **args, int argc, int line);
CppNode *cppnode_new_array(CppType *type, CppNode *size_expr, int line);
CppNode *cppnode_delete(CppNode *expr, int line);
CppNode *cppnode_delete_array(CppNode *expr, int line);
CppNode *cppnode_cast(CppType *type, CppNode *expr, int line);
CppNode *cppnode_sizeof_type(CppType *type, int line);
CppNode *cppnode_sizeof_expr(CppNode *expr, int line);
CppNode *cppnode_block(CppNode **stmts, int n_stmts, int line);
CppNode *cppnode_if(CppNode *cond, CppNode *then_, CppNode *else_, int line);
CppNode *cppnode_for(CppNode *init, CppNode *cond, CppNode *step, CppNode *body, int line);
CppNode *cppnode_while(CppNode *cond, CppNode *body, int line);
CppNode *cppnode_do_while(CppNode *body, CppNode *cond, int line);
CppNode *cppnode_switch(CppNode *expr, CppNode **cases, int n_cases, int line);
CppNode *cppnode_switch_case(CppNode *value, CppNode **stmts, int n_stmts, int line);
CppNode *cppnode_break(int line);
CppNode *cppnode_continue(int line);
CppNode *cppnode_return(CppNode *expr, int line);
CppNode *cppnode_var_decl(CppType *type, const char *name, CppNode *init, int line);
CppNode *cppnode_expr_stmt(CppNode *expr, int line);
CppNode *cppnode_unit(CppNode **decls, int n_decls, int line);
CppNode *cppnode_using_namespace(const char *name, int line);
CppNode *cppnode_using_decl(const char *qualified_name, int line);
CppNode *cppnode_namespace(const char *name, CppNode **decls, int n_decls, int line);
CppNode *cppnode_class_decl(const char *name, int is_struct, const char *base_class_name,
                             CppNode **template_params, int n_template_params,
                             CppNode **members, int n_members, int line);
CppNode *cppnode_field_decl(CppType *type, const char *name, CppNode *init, CppAccess access, int is_static, int line);
CppNode *cppnode_method_decl(CppType *ret_type, const char *name, CppNode **params, int n_params,
                              CppNode *body, CppAccess access, int is_static, int is_virtual, int is_const,
                              const char *op_name, int line);
CppNode *cppnode_ctor_decl(const char *name, CppNode **params, int n_params, CppNode *body,
                            CppNode **init_list_args, int n_init_args, const char *init_list_target,
                            CppAccess access, int line);
CppNode *cppnode_dtor_decl(const char *name, CppNode *body, CppAccess access, int is_virtual, int line);
CppNode *cppnode_func_decl(CppType *ret_type, const char *name, CppNode **template_params, int n_template_params,
                            CppNode **params, int n_params, CppNode *body, int line);
CppNode *cppnode_param(CppType *type, const char *name, CppNode *default_value, int line);
CppNode *cppnode_template_param(const char *name, int line);
CppNode *cppnode_include_raw(const char *text, int line);

void cppast_free(CppNode *n);
char *cpp_strdup(const char *s);

#endif /* CPP_AST_H */
