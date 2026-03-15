#ifndef CODEGEN_H
#define CODEGEN_H
#include "ast.h"
#include "assembler.h"
#include "symtable.h"
#include <stdint.h>

typedef struct {
    char *value;
    int   len;
    char *label;
    int   offset;
} StringEntry;

typedef struct {
    char *name;
    int   label_id;
} FuncRecord;

typedef struct {
    Assembler  *asm_;
    SymTable   *sym;
    int         is_64bit;

    StringEntry *strings;
    int          string_count;
    int          string_cap;
    int          string_pool_size;

    FuncRecord  *funcs;
    int          func_count;
    int          func_cap;

    /* Current function context */
    int          loop_end_label;   /* -1 = not in loop  */
    int          loop_top_label;
    int          switch_end_label; /* -1 = not in switch */
    int          cur_func_has_return;
} CodeGen;

void codegen_init    (CodeGen *cg, Assembler *a, SymTable *sym, int is_64bit);
void codegen_program (CodeGen *cg, ASTNode *prog);

/* Per-node code generation */
void codegen_func   (CodeGen *cg, ASTNode *n);
void codegen_stmt   (CodeGen *cg, ASTNode *n);
void codegen_expr   (CodeGen *cg, ASTNode *n);   /* result in RAX/EAX */
void codegen_lvalue (CodeGen *cg, ASTNode *n);   /* address in RAX/EAX */

/* Section data for PE builder */
uint8_t    *codegen_get_text  (CodeGen *cg, int *len);
uint8_t    *codegen_get_rdata (CodeGen *cg, int *len);
Relocation *codegen_get_relocs(CodeGen *cg, int *count);

#endif
