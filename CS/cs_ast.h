#ifndef CS_AST_H
#define CS_AST_H

/* =========================================================================
 * cs_ast — the C# AST (Phase 2 of the plan in
 * /home/squash/.claude/plans/nested-finding-walrus.md). Deliberately a
 * SEPARATE node/type shape from squash's own ast.h/ASTNode/TypeInfo (see
 * that file for the C-only shape this mirrors stylistically) — cs_lower.c
 * (Phase 3, not yet written) is what turns THIS tree into squash's real
 * ASTNode tree; nothing in codegen.c/codegen_arm64.c changes at all.
 *
 * Scope (matches cs_parser.c's own grammar coverage — see that file's
 * header comment for the authoritative list): classes/structs/interfaces,
 * fields, auto-properties, methods/constructors (incl. generic type
 * parameters + basic constraints), delegates via Func<>/Action<>-shaped
 * types + lambda expressions, standard control flow, try/catch/finally/
 * throw, string interpolation, LINQ METHOD syntax (needs no special
 * grammar at all — `list.Where(x => ...)` is just an ordinary member-call
 * chain with a lambda argument). LINQ QUERY syntax (`from x in y select
 * z`), async/await, unsafe, records, pattern matching beyond a plain
 * switch, attributes beyond parse-and-discard, and operator overloading
 * are explicitly NOT in this grammar yet (documented deferrals, not
 * silently missing) — see the plan's own "explicitly out of scope" list.
 * ========================================================================= */

typedef enum {
    /* Literals / primary expressions */
    CS_LIT_INT, CS_LIT_DOUBLE, CS_LIT_STRING, CS_LIT_INTERP_STRING,
    CS_LIT_BOOL, CS_LIT_NULL, CS_LIT_CHAR,
    CS_IDENT, CS_THIS, CS_BASE,
    /* Expressions */
    CS_ASSIGN, CS_BINARY, CS_UNARY, CS_TERNARY,
    CS_CALL, CS_MEMBER, CS_INDEX,
    CS_NEW_OBJECT, CS_NEW_ARRAY, CS_LAMBDA, CS_CAST,
    /* Statements */
    CS_BLOCK, CS_IF, CS_FOR, CS_FOREACH, CS_WHILE, CS_DO_WHILE,
    CS_SWITCH, CS_SWITCH_CASE, CS_BREAK, CS_CONTINUE, CS_RETURN,
    CS_TRY, CS_CATCH_CLAUSE, CS_THROW, CS_LOCAL_VAR_DECL, CS_EXPR_STMT,
    /* Members / top-level declarations */
    CS_UNIT, CS_USING, CS_NAMESPACE,
    CS_CLASS_DECL, CS_STRUCT_DECL, CS_INTERFACE_DECL,
    CS_FIELD_DECL, CS_PROPERTY_DECL, CS_METHOD_DECL, CS_CTOR_DECL,
    CS_PARAM, CS_TYPE_PARAM
} CsAstKind;

/* Type reference (NOT a declaration) — "List<Person>", "int", "string?",
 * "Person[]", "Func<int,int>", etc. `type_args` holds generic arguments
 * (empty for a non-generic type); `array_rank` counts trailing "[]" (only
 * rank is tracked, not per-dimension sizes — matches the plan's own array
 * scope). */
typedef struct CsType CsType;
struct CsType {
    char     *name;
    CsType  **type_args;  int n_type_args;
    int       array_rank;
    int       is_nullable;
};

CsType *cstype_new(const char *name);
CsType *cstype_copy(const CsType *t);
void    cstype_free(CsType *t);
char   *cstype_str(const CsType *t);   /* debug/mangled-name-ish rendering */

typedef struct CsNode CsNode;

/* One interpolated-string segment: either literal text (`is_expr`==0,
 * `text` set) or an embedded expression (`is_expr`==1, `expr` set —
 * itself a full expression, e.g. supports `$"{a.B(c)}"`). */
typedef struct { int is_expr; char *text; CsNode *expr; } CsInterpPart;

/* A generic type parameter with its (possibly empty) constraint list —
 * "where T : class", "where T : struct", "where T : new()", "where T :
 * SomeBase" all collapse to a small constraint-kind tag + optional base
 * type name, matching the plan's "basic constraints" scope. */
typedef enum { CS_CONSTRAINT_NONE, CS_CONSTRAINT_CLASS, CS_CONSTRAINT_STRUCT,
               CS_CONSTRAINT_NEW, CS_CONSTRAINT_BASE_TYPE } CsConstraintKind;
typedef struct { CsConstraintKind kind; char *base_type_name; } CsConstraint;

struct CsNode {
    CsAstKind kind;
    int       line;

    union {
        struct { long long value; }                     lit_int;
        struct { double value; }                        lit_double;
        struct { char *value; }                          lit_string;
        struct { CsInterpPart *parts; int n_parts; }      lit_interp;
        struct { int value; }                             lit_bool;
        struct { long long value; }                       lit_char;
        struct { char *name; }                             ident;

        struct { char *op; CsNode *lhs; CsNode *rhs; }    assign;
        struct { char *op; CsNode *left; CsNode *right; } binary;
        struct { char *op; CsNode *operand; int postfix; } unary;
        struct { CsNode *cond; CsNode *then_; CsNode *else_; } ternary;

        struct { CsNode *callee; CsNode **args; int argc; } call;
        struct { CsNode *obj; char *name; }                member;
        struct { CsNode *obj; CsNode *index; }              index_;

        struct { CsType *type; CsNode **args; int argc; }   new_object;
        struct { CsType *elem_type; CsNode **elems; int n_elems; CsNode *size_expr; } new_array;
        struct { char **param_names; int n_params; CsNode *body; int body_is_expr; } lambda;
        struct { CsType *type; CsNode *expr; }               cast;

        struct { CsNode **stmts; int n_stmts; }              block;
        struct { CsNode *cond; CsNode *then_; CsNode *else_; } if_;
        struct { CsNode *init; CsNode *cond; CsNode *step; CsNode *body; } for_;
        struct { CsType *elem_type; char *var_name; CsNode *collection; CsNode *body; } foreach_;
        struct { CsNode *cond; CsNode *body; }                while_;
        struct { CsNode *body; CsNode *cond; }                do_while;
        struct { CsNode *expr; CsNode **cases; int n_cases; } switch_;
        struct { CsNode **values; int n_values; int is_default; CsNode **stmts; int n_stmts; } switch_case;
        struct { CsNode *expr; }                              return_;
        struct { CsNode *try_block; CsNode **catches; int n_catches; CsNode *finally_block; } try_;
        struct { CsType *ex_type; char *var_name; CsNode *body; } catch_clause;
        struct { CsNode *expr; }                              throw_; /* expr==NULL => bare "throw;" rethrow */
        struct { CsType *type; char *name; CsNode *init; int is_var; } local_var_decl;
        struct { CsNode *expr; }                              expr_stmt;

        struct { CsNode **usings; int n_usings; CsNode **decls; int n_decls; } unit;
        struct { char *namespace_name; }                       using_;
        struct { char *name; CsNode **decls; int n_decls; }    namespace_decl;

        struct {
            char      *name;
            char      *base_class_name;      /* single base class, or NULL */
            char     **interface_names; int n_interfaces;
            CsNode   **type_params;  int n_type_params; /* CS_TYPE_PARAM nodes */
            CsNode   **members;      int n_members;
            int        is_static;
        } class_decl; /* also used for CS_STRUCT_DECL / CS_INTERFACE_DECL */

        struct { CsType *type; char *name; CsNode *init; int is_static; } field_decl;
        struct { CsType *type; char *name; int is_static; int has_setter; } property_decl;
        struct {
            CsType    *ret_type;
            char      *name;
            CsNode   **type_params; int n_type_params;
            CsNode   **params;      int n_params;
            CsNode    *body;        /* NULL = abstract/interface method */
            int        is_static;
            /* Phase 6c: "[DllImport("...")] static extern <ret> Name(...);"
             * -- NULL for every ordinary method. When set, this method has
             * no body (bodyless extern, same as any other body==NULL
             * method) but ALSO lowers to a direct call to a REAL native
             * symbol of this exact name (e.g. "vkCreateInstance"), not the
             * usual "Class__Method" mangling -- see cs_lower.c's own
             * comment at its use site. Only the DllImport string argument
             * is captured; the library name itself is accepted but not
             * used to pick a specific .so (see cs_parser.c's own comment
             * on why -- sqo_loader resolves everything through one flat
             * host symbol table today). */
            char      *dllimport_name;
        } method_decl;
        struct {
            char *name; CsNode **params; int n_params; CsNode *body;
            /* ": base(args)" constructor initializer -- NULL/0 if this
             * ctor has none (either no initializer at all, or a ": this
             * (...)" same-class chain, still not implemented -- see
             * cs_parser.c's own comment at the capture site). Lets
             * cs_lower.c actually run the base class's own field
             * initializers/ctor body (via a generated "<Base>__init(
             * this, args...)" helper -- see lower_class_methods' own
             * comment) instead of the base(...) call being silently
             * discarded, as it used to be. */
            CsNode **base_args; int n_base_args;
        } ctor_decl;
        /* is_out_ref: 1 if this parameter was declared "out"/"ref" -- for
         * an ordinary C# method these are still just parsed and otherwise
         * unimplemented (see cs_parser.c's own comment: real by-reference
         * parameter-passing for ordinary methods is out of scope). Only
         * used so far for a [DllImport] extern declaration's OWN
         * parameters (Phase 6c), where it matters a lot: real P/Invoke
         * marshals an "out"/"ref" parameter as a pointer to the caller's
         * storage automatically (e.g. Vulkan's own
         * "vkEnumerateInstanceVersion(uint32_t *pApiVersion)" -- an
         * "out uint apiVersion" C# parameter needs to become a REAL
         * uint32_t* argument, not a value copy), which cs_lower.c
         * implements for DllImport methods specifically -- see its own
         * use site. */
        struct { CsType *type; char *name; CsNode *default_value; int is_out_ref; } param;
        struct { char *name; CsConstraint *constraints; int n_constraints; } type_param;
    };
};

/* Constructors (one per kind actually built by cs_parser.c) */
CsNode *csnode_lit_int(long long v, int line);
CsNode *csnode_lit_double(double v, int line);
CsNode *csnode_lit_string(const char *v, int line);
CsNode *csnode_lit_interp(CsInterpPart *parts, int n_parts, int line);
CsNode *csnode_lit_bool(int v, int line);
CsNode *csnode_lit_null(int line);
CsNode *csnode_lit_char(long long v, int line);
CsNode *csnode_ident(const char *name, int line);
CsNode *csnode_this(int line);
CsNode *csnode_base(int line);
CsNode *csnode_assign(const char *op, CsNode *lhs, CsNode *rhs, int line);
CsNode *csnode_binary(const char *op, CsNode *l, CsNode *r, int line);
CsNode *csnode_unary(const char *op, CsNode *operand, int postfix, int line);
CsNode *csnode_ternary(CsNode *c, CsNode *t, CsNode *e, int line);
CsNode *csnode_call(CsNode *callee, CsNode **args, int argc, int line);
CsNode *csnode_member(CsNode *obj, const char *name, int line);
CsNode *csnode_index(CsNode *obj, CsNode *index, int line);
CsNode *csnode_new_object(CsType *type, CsNode **args, int argc, int line);
CsNode *csnode_new_array(CsType *elem_type, CsNode **elems, int n_elems, CsNode *size_expr, int line);
CsNode *csnode_lambda(char **param_names, int n_params, CsNode *body, int body_is_expr, int line);
CsNode *csnode_cast(CsType *type, CsNode *expr, int line);
CsNode *csnode_block(CsNode **stmts, int n_stmts, int line);
CsNode *csnode_if(CsNode *cond, CsNode *then_, CsNode *else_, int line);
CsNode *csnode_for(CsNode *init, CsNode *cond, CsNode *step, CsNode *body, int line);
CsNode *csnode_foreach(CsType *elem_type, const char *var_name, CsNode *collection, CsNode *body, int line);
CsNode *csnode_while(CsNode *cond, CsNode *body, int line);
CsNode *csnode_do_while(CsNode *body, CsNode *cond, int line);
CsNode *csnode_switch(CsNode *expr, CsNode **cases, int n_cases, int line);
CsNode *csnode_switch_case(CsNode **values, int n_values, int is_default, CsNode **stmts, int n_stmts, int line);
CsNode *csnode_break(int line);
CsNode *csnode_continue(int line);
CsNode *csnode_return(CsNode *expr, int line);
CsNode *csnode_try(CsNode *try_block, CsNode **catches, int n_catches, CsNode *finally_block, int line);
CsNode *csnode_catch_clause(CsType *ex_type, const char *var_name, CsNode *body, int line);
CsNode *csnode_throw(CsNode *expr, int line);
CsNode *csnode_local_var_decl(CsType *type, const char *name, CsNode *init, int is_var, int line);
CsNode *csnode_expr_stmt(CsNode *expr, int line);
CsNode *csnode_unit(CsNode **usings, int n_usings, CsNode **decls, int n_decls, int line);
CsNode *csnode_using(const char *ns, int line);
CsNode *csnode_namespace(const char *name, CsNode **decls, int n_decls, int line);
CsNode *csnode_class_decl(CsAstKind kind, const char *name, const char *base_class_name,
                           char **interface_names, int n_interfaces,
                           CsNode **type_params, int n_type_params,
                           CsNode **members, int n_members, int is_static, int line);
CsNode *csnode_field_decl(CsType *type, const char *name, CsNode *init, int is_static, int line);
CsNode *csnode_property_decl(CsType *type, const char *name, int is_static, int has_setter, int line);
CsNode *csnode_method_decl(CsType *ret_type, const char *name,
                            CsNode **type_params, int n_type_params,
                            CsNode **params, int n_params, CsNode *body, int is_static, int line);
CsNode *csnode_ctor_decl(const char *name, CsNode **params, int n_params, CsNode *body, int line);
CsNode *csnode_param(CsType *type, const char *name, CsNode *default_value, int line);
CsNode *csnode_type_param(const char *name, CsConstraint *constraints, int n_constraints, int line);

void csast_print(const CsNode *n, int indent);
void csast_free(CsNode *n);
char *cs_strdup(const char *s);

#endif /* CS_AST_H */
