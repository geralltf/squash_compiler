#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Register IDs (shared subset; 64-bit names used for 32-bit low-halves too)
 * ========================================================================= */
typedef enum {
    /* 64-bit general purpose */
    REG_RAX=0, REG_RCX, REG_RDX, REG_RBX,
    REG_RSP,   REG_RBP, REG_RSI, REG_RDI,
    REG_R8,    REG_R9,  REG_R10, REG_R11,
    REG_R12,   REG_R13, REG_R14, REG_R15,
    /* 32-bit aliases (same encoding, different REX) */
    REG_EAX=0, REG_ECX=1, REG_EDX=2, REG_EBX=3,
    REG_ESP=4, REG_EBP=5, REG_ESI=6, REG_EDI=7,
    REG_NONE = 255
} Reg;

/* =========================================================================
 * Condition codes
 * ========================================================================= */
typedef enum {
    CC_E=4, CC_NE=5, CC_L=12, CC_GE=13, CC_LE=14, CC_G=15,
    CC_B=2, CC_AE=3, CC_Z=4,  CC_NZ=5,
    CC_BE=6, CC_A=7  /* unsigned <=, unsigned > (CC_B/CC_AE above are unsigned </>=)  */
} CondCode;

/* =========================================================================
 * Relocation kinds
 * ========================================================================= */
typedef enum {
    RELOC_ABS32,       /* 32-bit absolute VA of IAT slot (32-bit PE)  */
    RELOC_DATA_ABS32,  /* 32-bit absolute VA of data label (32-bit PE) */
    RELOC_TEXT_ABS32,  /* 32-bit absolute VA of code label (32-bit PE) */
    RELOC_WDATA_ABS32, /* 32-bit absolute VA of writable-data label    */
    RELOC_REL32,       /* 32-bit PC-relative (branch/call)             */
    RELOC_IAT_REL32,   /* 32-bit RIP-relative to IAT slot (64-bit)    */
    RELOC_DATA_REL32,  /* 32-bit RIP-relative to data symbol (64-bit) */
    RELOC_WDATA_REL32,   /* 32-bit RIP-relative to writable-data symbol  */
    RELOC_STATIC_REL32,  /* 32-bit PC-relative call to static-linked function */
    /* ARM64 relocations (two instructions emitted per reference) */
    RELOC_A64_IAT_ADRP,    /* patch ADRP instruction to IAT slot page          */
    RELOC_A64_IAT_LO12,    /* patch LDR instruction bits[21:10] with IAT lo12  */
    RELOC_A64_DATA_ADRP,   /* patch ADRP instruction to .rdata string page     */
    RELOC_A64_DATA_LO12,   /* patch ADD instruction bits[21:10] with str lo12  */
    RELOC_A64_WDATA_ADRP,  /* patch ADRP instruction to .data wdata page       */
    RELOC_A64_WDATA_LO12,  /* patch ADD instruction bits[21:10] with wdata lo12*/
    RELOC_A64_STATIC_BL,   /* 26-bit BL immediate to a static-linked function
                             * (.sqo cross-object call, or a ".a" archive
                             * member) — AArch64 sibling of RELOC_STATIC_REL32,
                             * a single instruction rather than a two-part
                             * ADRP/LO12 pair since BL's own immediate field
                             * already reaches ±128MB. */
} RelocKind;

/* =========================================================================
 * A relocation entry: patch emitted code at 'offset' after linking
 * ========================================================================= */
typedef struct {
    int        offset;     /* offset in code buffer of the 4-byte field  */
    RelocKind  kind;
    char      *symbol;     /* import name "WriteFile" or data label       */
    int        addend;     /* value to add after resolving                */
} Relocation;

/* =========================================================================
 * Label (for jump targets)
 * ========================================================================= */
typedef struct {
    char *name;
    int   offset;   /* -1 = not yet defined (forward reference)          */
} Label;

/* =========================================================================
 * Fixup: a forward-reference branch that needs patching
 * ========================================================================= */
typedef struct {
    int   patch_offset;  /* where in code[] to write the 4-byte disp     */
    int   label_id;
} Fixup;

/* =========================================================================
 * Assembler context
 * ========================================================================= */
#define ASM_BUF_INIT  65536
#define ASM_MAX_RELOC 4096
#define ASM_MAX_LABEL 4096

/* ---- Static data symbol entry ---- */
typedef struct {
    char    *name;    /* symbol name for relocation */
    int      offset;  /* byte offset in data buffer */
    int      size;    /* size in bytes              */
} DataSymbol;

typedef struct {
    uint8_t   *code;
    int        code_len;
    int        code_cap;

    Relocation *relocs;
    int         reloc_count;
    int         reloc_cap;

    Label      *labels;
    int         label_count;
    int         label_cap;

    Fixup      *fixups;
    int         fixup_count;
    int         fixup_cap;

    /* Static / global variable data section */
    uint8_t   *data_buf;
    int        data_len;
    int        data_cap;
    DataSymbol *data_syms;
    int         data_sym_count;
    int         data_sym_cap;

    int         is_64bit;
    int         is_linux;  /* 1 = SysV ABI (Linux), 0 = Windows ABI */
    int         chkstk_movrax_off; /* offset of mov eax imm32 placeholder for __chkstk, -1 if none */
} Assembler;

/* =========================================================================
 * Core assembler API
 * ========================================================================= */
void asm_init   (Assembler *a, int is_64bit);
void asm_free   (Assembler *a);

/* Raw emission */
void asm_emit1  (Assembler *a, uint8_t b);
void asm_emit2  (Assembler *a, uint8_t b0, uint8_t b1);
void asm_emit3  (Assembler *a, uint8_t b0, uint8_t b1, uint8_t b2);
void asm_emit4  (Assembler *a, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3);
void asm_emit_u16(Assembler *a, uint16_t v);
void asm_emit_u32(Assembler *a, uint32_t v);
void asm_emit_u64(Assembler *a, uint64_t v);
void asm_emit_bytes(Assembler *a, const uint8_t *buf, int len);

/* Labels & fixups */
int  asm_new_label (Assembler *a, const char *name);  /* allocate label */
void asm_def_label (Assembler *a, int id);             /* define at current pos */
void asm_emit_fixup(Assembler *a, int label_id);       /* emit placeholder + register fixup */
void asm_resolve   (Assembler *a);                     /* patch all fixups */
void asm_resolve_text_relocs(Assembler *a, uint32_t text_rva, uint64_t image_base);

/* Relocations (for imports / data) */
void asm_reloc_iat (Assembler *a, const char *sym);    /* import call slot */
void asm_reloc_data (Assembler *a, const char *sym);   /* rdata label (read-only) */
void asm_reloc_wdata(Assembler *a, const char *sym);   /* wdata label (read-write) */

/* =========================================================================
 * High-level instruction emitters
 * ========================================================================= */

/* ---- Stack frame ---- */
void asm_push_reg   (Assembler *a, Reg r);
void asm_pop_reg    (Assembler *a, Reg r);
void asm_push_imm32 (Assembler *a, int32_t v);
void asm_sub_rsp    (Assembler *a, int32_t n);      /* sub rsp/esp, imm  */
void asm_add_rsp    (Assembler *a, int32_t n);      /* add rsp/esp, imm  */
void asm_enter      (Assembler *a, int local_bytes); /* push rbp; mov rbp,rsp; sub rsp,N */
/* Two-pass frame: emit prologue with placeholder, patch later */
int  asm_enter_deferred(Assembler *a, int chkstk_lbl); /* chkstk_lbl=-1 for no probe; returns sub rsp patch offset */
void asm_patch_frame   (Assembler *a, int patch_off, int aligned_size);
void asm_leave      (Assembler *a);                  /* leave             */
void asm_ret        (Assembler *a);                  /* ret               */
void asm_win64_callee_restore(Assembler *a);          /* pop callee-saved regs pushed by asm_enter_deferred (Win64 only) */

/* ---- Data movement ---- */
void asm_mov_reg_imm  (Assembler *a, Reg dst, long long imm);   /* mov reg, imm */
void asm_mov_reg_reg  (Assembler *a, Reg dst, Reg src);          /* mov reg, reg */
void asm_mov_mem_reg  (Assembler *a, Reg base, int disp, Reg src);
void asm_mov_mem32_reg(Assembler *a, Reg base, int disp, Reg src);
void asm_mov_mem8_reg (Assembler *a, Reg base, int disp, Reg src);/* [base+d]=src */
void asm_mov_reg_mem  (Assembler *a, Reg dst, Reg base, int disp);/* dst=[base+d] (64-bit) */
void asm_mov_reg32_mem(Assembler *a, Reg dst, Reg base, int disp);/* dst=[base+d] (32-bit, zero-extends) */
void asm_mov_eax_mem8  (Assembler *a, Reg base, int disp);        /* MOVZX EAX,byte[base+d]  */
void asm_movsx_rax_mem8(Assembler *a, Reg base, int disp);        /* MOVSX RAX,byte[base+d]  */
void asm_movsx_eax_mem8(Assembler *a, Reg base, int disp);        /* MOVSX EAX,byte[base+d]  */
void asm_movzx_eax_mem8(Assembler *a, Reg base, int disp);        /* MOVZX EAX,byte[base+d]  */
void asm_movsxd_rax_mem(Assembler *a, Reg base, int disp);        /* MOVSXD RAX,dword[base+d] */
void asm_lea_rip_data (Assembler *a, Reg dst, const char *sym);  /* lea reg,[rip+rdata_sym] */
void asm_lea_rip_wdata(Assembler *a, Reg dst, const char *sym);  /* lea reg,[rip+wdata_sym] */
void asm_lea_rip_static(Assembler *a, Reg dst, const char *sym); /* lea reg,[rip+sym] via RELOC_STATIC_REL32 (cross-object function address) */
void asm_lea_rbp_disp (Assembler *a, Reg dst, int disp);         /* lea reg,[rbp+d] */
/* Load address of a code-section function label into dst.
 * 64-bit: lea dst,[rip+label]  (RIP-relative)
 * 32-bit: mov dst, label_abs   (absolute VA, RELOC_TEXT_ABS32) */
void asm_load_func_addr(Assembler *a, Reg dst, int label_id);

/* ---- Arithmetic ---- */
void asm_add_reg_reg(Assembler *a, Reg dst, Reg src);
void asm_sub_reg_reg(Assembler *a, Reg dst, Reg src);
void asm_imul_reg_reg(Assembler *a, Reg dst, Reg src);
void asm_idiv_reg  (Assembler *a, Reg src);          /* cdq; idiv src     */
void asm_div_reg   (Assembler *a, Reg src);          /* xor edx,edx; div src (unsigned) */
void asm_neg_reg   (Assembler *a, Reg r);
void asm_not_reg   (Assembler *a, Reg r);

/* ---- Bitwise ---- */
void asm_and_reg_reg(Assembler *a, Reg dst, Reg src);
void asm_or_reg_reg (Assembler *a, Reg dst, Reg src);
void asm_xor_reg_reg(Assembler *a, Reg dst, Reg src);
void asm_shl_reg_cl (Assembler *a, Reg r);
void asm_shr_reg_cl (Assembler *a, Reg r);

/* ---- Comparison & logical ---- */
void asm_cmp_reg_reg(Assembler *a, Reg a_, Reg b);
void asm_test_reg_reg(Assembler *a, Reg a_, Reg b);
void asm_setcc_al   (Assembler *a, CondCode cc);     /* setcc al          */
void asm_movzx_rax_al(Assembler *a);                 /* movzx rax, al     */

/* ---- Control flow ---- */
void asm_jmp_label  (Assembler *a, int label_id);
void asm_call_label (Assembler *a, int label_id);
void asm_jcc_label  (Assembler *a, CondCode cc, int label_id);
void asm_call_import(Assembler *a, const char *sym);  /* call [IAT_sym]   */
void asm_load_import_addr(Assembler *a, Reg dst, const char *sym); /* mov dst,[rip+IAT_sym] — the import's real address as a VALUE, not a call */
void asm_call_direct(Assembler *a, int label_id);     /* call label        */
void asm_call_static(Assembler *a, const char *sym);  /* call rel32 → static-linked func */
void asm_int3       (Assembler *a);

/* ---- x64 Windows ABI helpers ---- */
/* Pass arg N (0-based) into the right register/stack slot.
   Value is already in RAX; this moves it to the right place. */
void asm_arg_from_rax(Assembler *a, int arg_index);
/* After all args are set, call an import */
void asm_call_import_with_args(Assembler *a, const char *sym, int argc);

/* ---- x86 32-bit helpers ---- */
void asm_call_import32(Assembler *a, const char *sym); /* call [abs_iat]  */

/* ---- call through register (function pointer) ---- */
/* call rax / call eax — register holds the function address */
void asm_call_reg(Assembler *a, Reg reg);

/* ---- Static / global data section ---- */
/* Append N zero bytes to the data section; returns offset. */
int  asm_data_alloc (Assembler *a, const char *name, int size);
/* Write a value into the data section at a given offset. */
void asm_data_write32(Assembler *a, int offset, int32_t value);
void asm_data_write64(Assembler *a, int offset, int64_t value);
/* Get pointer to raw data bytes and their length. */
uint8_t *asm_data_bytes(Assembler *a, int *out_len);

/* ---- x86 32-bit helpers ---- */
void asm_call_import32(Assembler *a, const char *sym); /* call [abs_iat]  */

/* ---- Inline-asm (GCC extended asm) support primitives ----
 * These exist to encode the actual mnemonics that appear in real-world
 * __asm__ templates (atomics, CPU feature probes, breakpoints) — not used
 * by squash's own codegen otherwise. */
void asm_lock_prefix    (Assembler *a);                    /* emits 0xF0 (LOCK prefix byte) */
void asm_xchg_reg_reg   (Assembler *a, Reg r1, Reg r2);     /* xchg r1, r2 — native/pointer width */
void asm_xchg_reg_mem   (Assembler *a, Reg reg, Reg base, int disp); /* xchg reg, [base+disp] — native width */
void asm_cmpxchg_mem_reg(Assembler *a, Reg base, int disp, Reg src); /* cmpxchg [base+disp], src — native width */
/* Width-explicit variants (width in bytes: 1, 2, 4, or 8). Prefer these for
 * inline-asm codegen, where operand width comes from the GAS mnemonic
 * suffix or the C variable's own declared type, NOT the assembler's target
 * mode — an "int" stays 4 bytes wide even when compiling for x86-64. */
void asm_xchg_reg_reg_w   (Assembler *a, Reg r1, Reg r2, int width);
void asm_xchg_reg_mem_w   (Assembler *a, Reg reg, Reg base, int disp, int width);
void asm_cmpxchg_mem_reg_w(Assembler *a, Reg base, int disp, Reg src, int width);
void asm_inc_reg_w        (Assembler *a, Reg r, int width);
void asm_dec_reg_w        (Assembler *a, Reg r, int width);
void asm_alu_reg_imm_w    (Assembler *a, int op, Reg r, long long imm, int width);
void asm_pushf          (Assembler *a);                    /* pushf/pushfq */
void asm_popf           (Assembler *a);                    /* popf/popfq   */
void asm_nop            (Assembler *a);
void asm_pause          (Assembler *a);                    /* f3 90        */
void asm_hlt            (Assembler *a);
void asm_cpuid          (Assembler *a);
void asm_rdtsc          (Assembler *a);
void asm_int_imm8       (Assembler *a, uint8_t n);          /* int $n       */
void asm_inc_reg        (Assembler *a, Reg r);
void asm_dec_reg        (Assembler *a, Reg r);
/* Group-1 ALU op reg,imm. op: 0=add,1=or,4=and,5=sub,6=xor,7=cmp (matches
 * the x86 opcode-extension field for 80/81/83, i.e. real machine encoding,
 * not an arbitrary enum). */
void asm_alu_reg_imm    (Assembler *a, int op, Reg r, long long imm);

#endif /* ASSEMBLER_H */
void asm_add_imm(Assembler *a, Reg dst, int imm);

/* =========================================================================
 * Floating-point SSE2 helpers (64-bit) and x87 helpers (32-bit)
 * All float values: XMM0 = result register, XMM1 = secondary
 * ========================================================================= */

/* 64-bit SSE2 double operations */
void asm_movsd_load (Assembler *a, int xmm_dst, Reg base, int disp); /* movsd xmm,[base+disp] */
void asm_movsd_store(Assembler *a, Reg base, int disp, int xmm_src); /* movsd [base+disp],xmm */
void asm_movsd_rip  (Assembler *a, int xmm_dst, const char *sym);    /* movsd xmm,[rip+sym] */
void asm_movsd_xmm  (Assembler *a, int dst, int src);                 /* movsd xmm,xmm */
void asm_addsd      (Assembler *a, int dst, int src);
void asm_subsd      (Assembler *a, int dst, int src);
void asm_mulsd      (Assembler *a, int dst, int src);
void asm_divsd      (Assembler *a, int dst, int src);
void asm_ucomisd    (Assembler *a, int xmm0, int xmm1);
void asm_cvtsi2sd   (Assembler *a, int xmm_dst, Reg int_src); /* int->double */
void asm_cvttsd2si  (Assembler *a, Reg int_dst, int xmm_src); /* double->int (truncate) */
void asm_movss_load (Assembler *a, int xmm_dst, Reg base, int disp); /* float (32-bit) */
void asm_movss_store(Assembler *a, Reg base, int disp, int xmm_src);
void asm_cvtss2sd   (Assembler *a, int xmm_dst, int xmm_src); /* float->double */
void asm_cvtsd2ss   (Assembler *a, int xmm_dst, int xmm_src); /* double->float */

/* Push/pop XMM registers (via stack) */
void asm_push_xmm   (Assembler *a, int xmm);  /* sub rsp,8; movsd [rsp],xmm */
void asm_pop_xmm    (Assembler *a, int xmm);  /* movsd xmm,[rsp]; add rsp,8 */

/* XOR of float/double (for obfuscation/misc use) */
void asm_xorpd      (Assembler *a, int dst, int src); /* xorpd xmm,xmm */
void asm_xorps      (Assembler *a, int dst, int src); /* xorps xmm,xmm */

/* 32-bit x87 helpers */
void asm_fld_mem64  (Assembler *a, Reg base, int disp); /* fldl [base+disp] */
void asm_fstp_mem64 (Assembler *a, Reg base, int disp); /* fstpl [base+disp] */
void asm_fild_mem32 (Assembler *a, Reg base, int disp); /* fildl [base+disp] */
void asm_fistp_mem32(Assembler *a, Reg base, int disp); /* fistpl [base+disp] */
void asm_faddp      (Assembler *a);
void asm_fsubp      (Assembler *a);
void asm_fsubrp     (Assembler *a);
void asm_fmulp      (Assembler *a);
void asm_fdivp      (Assembler *a);
void asm_fdivrp     (Assembler *a);
void asm_fcompp     (Assembler *a);
void asm_fnstsw     (Assembler *a);  /* fnstsw ax */
void asm_sahf       (Assembler *a);  /* sahf */
void asm_fld1       (Assembler *a);  /* fld1 */
void asm_fldz       (Assembler *a);  /* fldz */
void asm_fldpi      (Assembler *a);  /* fldpi */
void asm_fchs       (Assembler *a);  /* fchs (negate) */
void asm_fabs_x87   (Assembler *a);  /* fabs */
void asm_fsqrt      (Assembler *a);  /* fsqrt */
