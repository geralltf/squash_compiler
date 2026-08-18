#ifndef SYMTABLE_H
#define SYMTABLE_H
#include "ast.h"

/* Number of 8-byte words reserved below RBP, on 64-bit SysV targets (real
 * Linux and macOS -- both is_64bit&&is_linux), for the SysV callee-saved
 * GPRs (RBX, R12-R15) that asm_enter_deferred()/asm_win64_callee_restore()
 * (assembler.c) push/restore at function entry/exit -- 5 registers + 1
 * padding slot to keep RSP's parity invariant intact, matching the
 * Win64 branch's own identical padding reasoning. Real SysV ABI requires
 * a callee to preserve these across any call, including a squash-compiled
 * function invoked directly as a callback by real external C code (e.g.
 * qsort()'s comparator, or a CoreAudio AudioQueueOutputCallback) -- squash's
 * own codegen uses these registers freely as scratch throughout function
 * bodies (invisible for squash-to-squash calls, where both sides agree on
 * the convention, and for squash calling OUT to a real library, which
 * follows the real ABI and preserves whatever IT uses) -- confirmed via a
 * minimal standalone repro: a real qsort() call with a comparator with a
 * few ordinary int locals corrupted the comparator's OWN first parameter
 * before ever reading it, reliably, on every run. SYM_PARAM's register-
 * argument home offsets (symtable_define_param) and the matching codegen.c
 * prologue/local-variable-start offsets must all reserve this same space
 * so parameter/local storage never overlaps the pushed register area. */
#define SQ_SYSV_CALLEE_SAVE_WORDS 6

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
    int       array_size2;  /* second dimension for T x[N][M]; 0 = not 2D */
    int       slot_size;    /* for SYM_VAR: the aligned stack slot size symtable_define_var
                                reserved (used by the ARM64 backend to correct for its
                                upward, FP-relative addressing — see codegen_arm64.c) */
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
    int      is_linux;
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
Symbol *symtable_define_param (SymTable *st, const char *name, TypeInfo *type, int idx, int byte_offset_32);
Symbol *symtable_define_func  (SymTable *st, const char *name, TypeInfo *ret, int paramc, ASTNode *node);
Symbol *symtable_define_import(SymTable *st, const char *name, const char *dll);
Symbol *symtable_define_import_typed(SymTable *st, const char *name, const char *dll, TypeInfo *rettype);
Symbol *symtable_define_enum_val(SymTable *st, const char *name, long long val);
Symbol *symtable_define_typedef(SymTable *st, const char *name, TypeInfo *type);
Symbol *symtable_define_struct (SymTable *st, const char *name, ASTNode *node, int sz);
Symbol *symtable_lookup       (SymTable *st, const char *name);
int     symtable_sizeof_struct (SymTable *st, ASTNode *struct_node);
int     symtable_compute_struct_alignment(SymTable *st, ASTNode *struct_node);
ASTNode *symtable_resolve_struct_node(SymTable *st, const char *type_name);
int     symtable_field_offset (SymTable *st, ASTNode *struct_node, const char *field_name,
                                TypeInfo **out_type, int *out_elem_size);
void    symtable_reset_locals (SymTable *st);
int     symtable_local_size   (SymTable *st);
void    symtable_add_import   (SymTable *st, const char *dll_func);
void    symtable_print        (const SymTable *st);
const char *symtable_find_dll (SymTable *st, const char *name);

#endif
