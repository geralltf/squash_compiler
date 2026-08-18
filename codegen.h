#ifndef CODEGEN_H
#define CODEGEN_H
#include "ast.h"
#include "assembler.h"
#include "symtable.h"
#include "linker.h"
#include <stdint.h>

typedef struct {
    char *value;
    int   len;
    char *label;
    int   offset;
    int   is_wide;  /* value holds raw UTF-16LE bytes (len includes the
                     * 2-byte null terminator), not a NUL-terminated C
                     * string — L"..." literals, see intern_string(). */
} StringEntry;

/* Writable data entry — lives in .data (R/W) section, not .rdata */
typedef struct {
    char *label;    /* symbol name for relocation */
    int   offset;   /* byte offset within the wdata pool */
    int   size;     /* size in bytes */
    uint8_t *content; /* NULL = zero-initialised (the common case); otherwise
                        * `size` real initial bytes, owned by this entry —
                        * used for "static [const] T x[] = {...};" locals
                        * with a compile-time-constant initializer (matches
                        * real compilers: these need no runtime init code at
                        * all, just real initial data placed in .data, same
                        * as a global). */
} WDataEntry;

typedef struct {
    char *name;
    int   label_id;
} FuncRecord;

typedef struct {
    Assembler     *asm_;
    SymTable      *sym;
    LinkerContext *linker;  /* NULL if no user libraries; Linux only */
    int            is_64bit;
    int            is_linux;  /* 1 = Linux/SysV target */

    /* 1 if this build is also linking in one or more ".sqo" cross-object
     * files (compiler.c's n_obj>0) -- mirrors codegen_arm64.h's identical
     * field; see its own comment on a64_emit_linux_extern_call for exactly
     * what this changes (an otherwise-unresolved bodyless call prefers a
     * deferred cross-object static call over the historical "assume
     * libc.so.6" GOT-import default only when this is set). */
    int            prefer_static_calls;

    /* The actual export-name tables of every ".sqo" file this build is
     * linking in (compiler.c reads their headers before codegen runs, just
     * to populate this — the real objfile_merge() happens later, after
     * codegen, against a fresh read). prefer_static_calls alone only means
     * "some .sqo is present"; it says nothing about whether THIS PARTICULAR
     * bodyless call's name is actually one of their exports. Guessing wrong
     * (unconditionally preferring a static call whenever prefer_static_calls
     * is set) breaks any genuine external libc call in the very same file —
     * confirmed via squash self-hosting itself for -macos: compiler.c's own
     * write() call (from stdio.h's fprintf shim) was misrouted as a
     * cross-object RELOC_STATIC_REL32 the moment n_obj>0, and never
     * resolved since no .sqo defines write(). See codegen_is_sqo_export(). */
    char         **sqo_export_names;
    int            sqo_export_count;

    /* 1 if THIS compile is itself producing a ".sqo" (compiler.c's -c /
     * compile_only) rather than linking existing ones in. -c and ".sqo"
     * linking are mutually exclusive (compiler.c rejects both together),
     * so sqo_export_names is necessarily empty here — there is no sibling
     * object to check a name against yet, because those siblings haven't
     * been compiled yet either (that's the whole point of a separate -c
     * compile). For a plain (non-"extern"-keyword) bodyless SYM_FUNC call
     * this means codegen_is_sqo_export() can never confirm anything, but
     * unlike an "extern"-declared name (overwhelmingly a real external
     * function — see the dll=="extern" branches, which do NOT consult this
     * flag), a plain bodyless prototype with no known-shim name is
     * overwhelmingly a same-program helper meant to be resolved by
     * whichever OTHER translation unit ends up defining it (confirmed via
     * squash self-hosting itself: lexer.c calls my_strdup(), defined only
     * in ast.c — every *.c file declares it as a bare bodyless prototype).
     * Speculatively emitting a deferred RELOC_STATIC_REL32 here (instead of
     * guessing "libc") is exactly what objfile.h's cross-object model was
     * built for: right, if some other linked-in ".sqo" really does define
     * it; a soft, already-existing "unresolved static symbol" warning at
     * final link time if it genuinely doesn't. */
    int            sqo_precompile;

    StringEntry *strings;
    int          string_count;
    int          string_cap;
    int          string_pool_size;

    FuncRecord  *funcs;
    int          func_count;
    int          func_cap;

    /* Current function context */
    const char  *cur_func_name;    /* name of the function codegen is currently
                                       inside, or NULL — diagnostics-only context
                                       (see diag.c), not used by any codegen
                                       logic itself. */
    int          loop_end_label;   /* -1 = not in loop  */
    int          loop_top_label;
    int          switch_end_label; /* -1 = not in switch */
    int          cur_func_has_return;
    int          cur_func_ret_is_float32; /* 1 if current function's declared
                                              return type is "float" (32-bit) —
                                              AST_RETURN narrows codegen_float_expr's
                                              double-precision XMM0 result with
                                              cvtsd2ss when set. */

    /* Writable data pool (.data section) — for mutable runtime values.
     * Used for: stdout HANDLE cache, static local variables.
     * Must be in .data (R/W), NOT .rdata (R only).                         */
    WDataEntry  *wdata;           /* writable data entries                  */
    int          wdata_count;
    int          wdata_cap;
    int          wdata_pool_size; /* total bytes in the wdata pool          */

    /* Float constant pool — 8-byte double literals in .rdata              */
    StringEntry *float_consts;
    int          float_const_count;
    int          float_const_cap;
    int          float_const_pool_size;

    /* Cached stdout HANDLE slot — allocated in .data (writable).           */
    char         stdout_handle_lbl[64];
    int          write_stdout_lbl;  /* label for shared __write_stdout helper, 0=not yet emitted */

    /* Label ID of the embedded __chkstk_probe helper (-1 = not yet emitted) */
    int          chkstk_lbl;
} CodeGen;

void codegen_init    (CodeGen *cg, Assembler *a, SymTable *sym, int is_64bit);
void codegen_program (CodeGen *cg, ASTNode *prog);

/* True for the fixed set of CRT-shaped names (malloc, printf, memcpy, ...)
 * this compiler always resolves specially (either via its own inline Win32
 * shim on x86-64, or via an always-assumed libc.so.6 GOT import on ARM64 —
 * see codegen_arm64.c's a64_emit_linux_extern_call) rather than treating as
 * a plain external/cross-object reference. */
int  is_internal_shim(const char *name);

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

/* Float codegen — evaluates float expressions into XMM0 (64-bit) or ST0 (32-bit) */
/* Returns 1 if the expression is a float type, 0 if integer */
int  codegen_is_float_expr(CodeGen *cg, ASTNode *n);
void codegen_float_expr    (CodeGen *cg, ASTNode *n); /* result in XMM0/ST0 */
void codegen_store_float   (CodeGen *cg, ASTNode *lhs); /* store XMM0/ST0 to lvalue */

/* Float constant pool — 8-byte entries in .rdata for double literals */
const char *intern_float_const(CodeGen *cg, const char *val_bytes);

/* Extended wdata access for wdata pool query */
uint8_t    *codegen_get_wdata  (CodeGen *cg, int *len);
char      **codegen_get_wdata_labels (CodeGen *cg, int *count);
int        *codegen_get_wdata_offsets(CodeGen *cg, int *count);
