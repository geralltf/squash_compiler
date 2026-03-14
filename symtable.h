#ifndef SYMTABLE_H
#define SYMTABLE_H
#include "ast.h"

typedef enum {
    SYM_VAR,      /* local variable  */
    SYM_PARAM,    /* function param  */
    SYM_FUNC,     /* function        */
    SYM_IMPORT,   /* Windows API     */
    SYM_ENUM_VAL, /* enum constant   */
    SYM_TYPEDEF,  /* typedef alias   */
    SYM_STRUCT,   /* struct/union tag*/
    SYM_GLOBAL,   /* global variable */
} SymKind;

typedef struct Symbol Symbol;
struct Symbol {
    char     *name;
    SymKind   kind;
    TypeInfo *type;
    int       offset;       /* stack offset (local) or section offset (global) */
    int       param_index;
    int       paramc;
    long long enum_value;   /* for SYM_ENUM_VAL */
    char     *dll;          /* for SYM_IMPORT */
    int       is_64bit;
    int       array_size;
    int       struct_size;  /* for SYM_STRUCT */
    ASTNode  *struct_node;  /* for SYM_STRUCT — the full struct_decl */
    ASTNode  *func_node;    /* for SYM_FUNC   — the func_decl        */
    Symbol   *next;
};

typedef struct Scope Scope;
struct Scope {
    Symbol *head;
    Scope  *parent;
};

typedef struct {
    Scope   *current;
    int      next_offset;
    int      is_64bit;
    /* Imports used — for PE builder */
    char   **imports;
    int      import_count;
    int      import_cap;
} SymTable;

void    symtable_init         (SymTable *st, int is_64bit);
void    symtable_push_scope   (SymTable *st);
void    symtable_pop_scope    (SymTable *st);
Symbol *symtable_define_var   (SymTable *st, const char *name, TypeInfo *type);
Symbol *symtable_define_global(SymTable *st, const char *name, TypeInfo *type, int array_size);
Symbol *symtable_define_param (SymTable *st, const char *name, TypeInfo *type, int idx);
Symbol *symtable_define_func  (SymTable *st, const char *name, TypeInfo *ret, int paramc, ASTNode *node);
Symbol *symtable_define_import(SymTable *st, const char *name, const char *dll);
Symbol *symtable_define_enum_val(SymTable *st, const char *name, long long val);
Symbol *symtable_define_typedef(SymTable *st, const char *name, TypeInfo *type);
Symbol *symtable_define_struct (SymTable *st, const char *name, ASTNode *node, int sz);
Symbol *symtable_lookup       (SymTable *st, const char *name);
void    symtable_reset_locals (SymTable *st);
int     symtable_local_size   (SymTable *st);
void    symtable_add_import   (SymTable *st, const char *dll_func);
void    symtable_print        (const SymTable *st);

#endif
