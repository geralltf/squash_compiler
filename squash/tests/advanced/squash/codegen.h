#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"
#include "assembler.h"
#include "symtable.h"
#include <stdint.h>

/* =========================================================================
 * String literal pool (goes into .rdata)
 * ========================================================================= */
typedef struct {
    char *value;    /* raw string bytes (with escapes processed)     */
    int   len;      /* byte length including null terminator         */
    char *label;    /* label name like "str0", "str1", ...           */
    int   offset;   /* byte offset within .rdata string area         */
} StringEntry;

/* =========================================================================
 * Function record (for call label resolution)
 * ========================================================================= */
typedef struct {
    char *name;
    int   label_id;
    int   code_offset;   /* resolved after codegen                   */
} FuncRecord;

/* =========================================================================
 * Code generator context
 * ========================================================================= */
typedef struct {
    Assembler  *asm_;
    SymTable   *sym;
    int         is_64bit;

    /* String pool */
    StringEntry *strings;
    int          string_count;
    int          string_cap;
    int          string_pool_size;  /* total bytes used                */

    /* Function records */
    FuncRecord  *funcs;
    int          func_count;
    int          func_cap;

    /* Current function state */
    int          local_bytes;     /* size of local variable area      */
    int          in_func;
    char        *cur_func_name;

    /* Loop labels for break/continue (simple single-level) */
    int          loop_end_label;   /* -1 if not in loop               */
    int          loop_top_label;
} CodeGen;

/* =========================================================================
 * API
 * ========================================================================= */
void codegen_init    (CodeGen *cg, Assembler *a, SymTable *sym, int is_64bit);
void codegen_program (CodeGen *cg, ASTNode *prog);

/* Section data accessors for PE builder */
uint8_t *codegen_get_text    (CodeGen *cg, int *len);
uint8_t *codegen_get_rdata   (CodeGen *cg, int *len);
Relocation *codegen_get_relocs(CodeGen *cg, int *count);

/* Per-node traversal (public for testability) */
void codegen_func   (CodeGen *cg, ASTNode *n);
void codegen_stmt   (CodeGen *cg, ASTNode *n);
void codegen_expr   (CodeGen *cg, ASTNode *n);  /* result in RAX/EAX */

#endif /* CODEGEN_H */
