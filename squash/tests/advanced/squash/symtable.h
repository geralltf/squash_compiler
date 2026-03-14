#ifndef SYMTABLE_H
#define SYMTABLE_H

/* =========================================================================
 * Symbol kinds
 * ========================================================================= */
typedef enum {
    SYM_VAR,       /* local variable: stack offset from RBP/EBP */
    SYM_PARAM,     /* function parameter                        */
    SYM_FUNC,      /* function (may be external/imported)       */
    SYM_IMPORT,    /* external Windows API import               */
    SYM_STRING,    /* string literal (in .rdata)                */
} SymKind;

/* =========================================================================
 * One symbol entry
 * ========================================================================= */
typedef struct Symbol Symbol;
struct Symbol {
    char     *name;
    SymKind   kind;
    char     *type_name;     /* "int", "void", "char", ...            */
    int       offset;        /* stack offset (negative from RBP)      */
    int       param_index;   /* 0-based parameter index               */
    int       paramc;        /* number of params (SYM_FUNC)           */
    char     *dll;           /* DLL name for SYM_IMPORT               */
    int       is_64bit;      /* 1 = 64-bit target                     */
    Symbol   *next;          /* intrusive linked list (scope chain)   */
};

/* =========================================================================
 * Scope — a single level in the symbol stack
 * ========================================================================= */
typedef struct Scope Scope;
struct Scope {
    Symbol *head;
    Scope  *parent;
};

/* =========================================================================
 * Symbol table (manages scopes + global imports)
 * ========================================================================= */
typedef struct {
    Scope  *current;       /* innermost scope                         */
    int     next_offset;   /* next available stack slot (decreasing)  */
    int     is_64bit;
    /* import list for PE builder */
    char  **imports;       /* DLL-qualified names "kernel32:WriteFile" */
    int     import_count;
    int     import_cap;
} SymTable;

/* =========================================================================
 * API
 * ========================================================================= */
void    symtable_init       (SymTable *st, int is_64bit);
void    symtable_push_scope (SymTable *st);
void    symtable_pop_scope  (SymTable *st);

Symbol *symtable_define_var   (SymTable *st, const char *name, const char *type);
Symbol *symtable_define_param (SymTable *st, const char *name, const char *type, int idx);
Symbol *symtable_define_func  (SymTable *st, const char *name, const char *ret_type, int paramc);
Symbol *symtable_define_import(SymTable *st, const char *name, const char *dll);
Symbol *symtable_lookup       (SymTable *st, const char *name);

void    symtable_reset_locals  (SymTable *st);   /* between function bodies  */
int     symtable_local_size    (SymTable *st);   /* bytes needed on stack    */

void    symtable_add_import    (SymTable *st, const char *dll_func);
void    symtable_print         (const SymTable *st);

#endif /* SYMTABLE_H */
