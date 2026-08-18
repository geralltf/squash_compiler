#ifndef CODEGEN_ARM64_H
#define CODEGEN_ARM64_H

#include "arm64_asm.h"
#include "ast.h"
#include "symtable.h"
#include "linker.h"
#include <stdint.h>

/* =========================================================================
 * String/wdata entries — same structure as codegen.h
 * ========================================================================= */
typedef struct {
    char *value;
    int   len;
    char *label;
    int   offset;
} A64StringEntry;

typedef struct {
    char *label;
    int   offset;
    int   size;
} A64WDataEntry;

/* =========================================================================
 * ARM64 code generator state
 * ========================================================================= */
#define A64_MAX_TEMP_DEPTH 24   /* max nested expression temp slots */
/* Max chunk usable for both STP-pre (range -512..504) and LDP-post
 * (range -512..504) in a single instruction: must be <=504 (post-index
 * positive cap) and 16-aligned for stack alignment. 496 satisfies both. */
#define A64_FRAME_SPLIT 496

typedef struct {
    Arm64Asm  *asm_;
    SymTable  *sym;
    int        is_linux;

    /* Linux -l/.a resolution context (NULL if no user libraries were
     * specified) — mirrors codegen.h's CodeGen.linker; see a64_emit_call's
     * use of linker_lookup_dynamic/linker_link_static/linker_has_static_def. */
    LinkerContext *linker;

    /* 1 if this build is also linking in one or more ".sqo" cross-object
     * files (compiler.c's n_obj>0) — see a64_emit_linux_extern_call's own
     * comment for exactly what this changes. */
    int prefer_static_calls;

    /* Mirrors codegen.h's CodeGen.sqo_export_names — see its comment for
     * why prefer_static_calls alone isn't enough to safely pick a static
     * call. */
    char **sqo_export_names;
    int    sqo_export_count;

    /* String pool (read-only) */
    A64StringEntry *strings;
    int             string_count;
    int             string_cap;
    int             string_pool_size;

    /* Float constant pool (8-byte doubles in .rdata) */
    A64StringEntry *float_consts;
    int             float_const_count;
    int             float_const_cap;
    int             float_const_pool_size;

    /* Writable data pool (.data) */
    A64WDataEntry  *wdata;
    int             wdata_count;
    int             wdata_cap;
    int             wdata_pool_size;

    /* Current function context */
    int  stp_patch_off;      /* code offset of prologue placeholder to patch */
    int  sub_patch_off;      /* code offset of SUB sp, sp, #N (large frame) */
    int  cur_local_off;      /* current local var allocation (offset from x29 base 16) */
    int  temp_base;          /* frame offset where temp slots start */
    int  cur_temp_depth;     /* current temp stack depth */
    int  max_temp_depth;     /* peak temp depth for this function */
    int  x19_slot;           /* frame offset of saved x19 (fixed once by the prologue;
                                every epilogue — early return or fallthrough — must
                                use this same value, not recompute it) */
    int  frame_size;         /* total stack frame size, fixed once by the prologue */

    int  loop_end_label;     /* -1 = not in loop */
    int  loop_top_label;
    int  switch_end_label;   /* -1 = not in switch */
    int  cur_func_has_return;
    int  cur_func_is_float_ret; /* current function's declared return type is float/double —
                                    determines whether `return` leaves the result in d0 or x0 */
    int  static_var_counter; /* unique suffix for mangled static-local wdata labels */

    /* stdout wdata slot for Windows (matches codegen.c stdout_handle_lbl) */
    char stdout_handle_lbl[64];
    int  write_stdout_lbl;
} CodeGenA64;

/* =========================================================================
 * API
 * ========================================================================= */
void a64_codegen_init   (CodeGenA64 *cg, Arm64Asm *a, SymTable *sym, int is_linux, LinkerContext *linker, int prefer_static_calls);
void a64_codegen_program(CodeGenA64 *cg, ASTNode *prog);

uint8_t    *a64_codegen_get_text  (CodeGenA64 *cg, int *len);
uint8_t    *a64_codegen_get_rdata (CodeGenA64 *cg, int *len);
Relocation *a64_codegen_get_relocs(CodeGenA64 *cg, int *count);

#endif /* CODEGEN_ARM64_H */
