#ifndef ARM64_ASM_H
#define ARM64_ASM_H

#include <stdint.h>
#include <stddef.h>
#include "assembler.h"   /* Relocation, Label, Fixup types */

/* =========================================================================
 * ARM64 (AArch64) general-purpose register numbers
 * x0-x7:   argument / scratch
 * x8:      indirect result / scratch
 * x9-x15:  caller-saved scratch
 * x16-x17: intra-procedure-call scratch (IP0/IP1)
 * x18:     platform register (avoid)
 * x19-x28: callee-saved
 * x29:     frame pointer (FP)
 * x30:     link register (LR)
 * 31:      XZR (as source/dest) or SP (in address position)
 * ========================================================================= */
#define A64_X0   0
#define A64_X1   1
#define A64_X2   2
#define A64_X3   3
#define A64_X4   4
#define A64_X5   5
#define A64_X6   6
#define A64_X7   7
#define A64_X8   8
#define A64_X9   9
#define A64_X10  10
#define A64_X11  11
#define A64_X12  12
#define A64_X13  13
#define A64_X14  14
#define A64_X15  15
#define A64_X16  16   /* IP0 — used for IAT call indirection */
#define A64_X17  17   /* IP1 */
#define A64_X18  18
#define A64_X19  19   /* callee-saved scratch */
#define A64_X20  20
#define A64_X21  21
#define A64_X22  22
#define A64_X23  23
#define A64_X24  24
#define A64_X25  25
#define A64_X26  26
#define A64_X27  27
#define A64_X28  28
#define A64_X29  29   /* frame pointer */
#define A64_X30  30   /* link register */
#define A64_XZR  31   /* zero register (as source/dest) */
#define A64_SP   31   /* stack pointer (in address position) */
#define A64_FP   A64_X29
#define A64_LR   A64_X30
#define A64_IP0  A64_X16
#define A64_IP1  A64_X17

/* FP/SIMD double register numbers (D registers, 0-31) */
#define A64_D0   0
#define A64_D1   1
#define A64_D2   2
#define A64_D3   3
#define A64_D4   4
#define A64_D5   5
#define A64_D6   6
#define A64_D7   7
#define A64_D8   8    /* callee-saved FP */

/* =========================================================================
 * ARM64 condition codes (4-bit values for B.cond / CSET / CSINC)
 * ========================================================================= */
#define A64_EQ   0   /* equal                 (Z=1) */
#define A64_NE   1   /* not equal             (Z=0) */
#define A64_CS   2   /* carry set / unsigned >= */
#define A64_CC   3   /* carry clear / unsigned < */
#define A64_MI   4   /* negative              (N=1) */
#define A64_PL   5   /* positive or zero      (N=0) */
#define A64_VS   6   /* overflow / FP unordered */
#define A64_VC   7   /* no overflow */
#define A64_HI   8   /* unsigned >            (C=1 && Z=0) */
#define A64_LS   9   /* unsigned <=           (C=0 || Z=1) */
#define A64_GE  10   /* signed >=             (N==V) */
#define A64_LT  11   /* signed <              (N!=V) */
#define A64_GT  12   /* signed >              (Z=0 && N==V) */
#define A64_LE  13   /* signed <=             (Z=1 || N!=V) */
#define A64_AL  14   /* always */

/* =========================================================================
 * Arm64Asm context — parallel to Assembler but for AArch64
 * ========================================================================= */
typedef struct {
    uint8_t    *code;
    int         code_len;
    int         code_cap;

    Relocation *relocs;
    int         reloc_count;
    int         reloc_cap;

    Label      *labels;
    int         label_count;
    int         label_cap;

    Fixup      *fixups;
    int         fixup_count;
    int         fixup_cap;

    /* Read-only data pool (.rdata / .rodata) — strings, float literals */
    uint8_t    *rdata;
    int         rdata_len;
    int         rdata_cap;

    /* Writable data pool (.data) — mutable slots */
    uint8_t    *wdata;
    int         wdata_len;
    int         wdata_cap;

    int         is_linux;
} Arm64Asm;

/* =========================================================================
 * Init / utility
 * ========================================================================= */
void a64_asm_init(Arm64Asm *a, int is_linux);
void a64_asm_free(Arm64Asm *a);

/* Emit a 32-bit ARM64 instruction (little-endian) */
void a64_emit(Arm64Asm *a, uint32_t instr);

/* Patch a previously emitted instruction at code[off] */
void a64_patch(Arm64Asm *a, int off, uint32_t instr);

/* =========================================================================
 * Label / fixup management
 * ========================================================================= */
int  a64_new_label (Arm64Asm *a, const char *name); /* returns label id */
void a64_def_label (Arm64Asm *a, int id);            /* define label at current PC */
void a64_add_fixup (Arm64Asm *a, int patch_off, int label_id); /* branch fixup */
void a64_resolve   (Arm64Asm *a);                    /* resolve all fixups */
int  a64_find_label(Arm64Asm *a, const char *name);  /* -1 if not found */

/* =========================================================================
 * Relocation helpers
 * ========================================================================= */
void a64_add_reloc(Arm64Asm *a, int off, RelocKind kind, const char *sym, int addend);

/* Emit a placeholder BL plus a RELOC_A64_STATIC_BL relocation naming `sym` —
 * the AArch64 sibling of x86-64's asm_call_static(): a call to a function
 * not defined in this translation unit, resolved later by name (a ".sqo"
 * cross-object call via objfile_merge(), or a ".a" archive member via
 * LinkerContext) instead of a same-file label fixup. */
void a64_call_static(Arm64Asm *a, const char *sym);

/* =========================================================================
 * Instruction encoders
 * (inline static for performance — caller computes the uint32_t and calls a64_emit)
 * ========================================================================= */

/* --- Move / load immediate --- */
/* MOVZ Xd, #imm16, LSL #shift (shift in {0,16,32,48}) */
static uint32_t a64_MOVZ(int rd, unsigned imm16, int shift) {
    return 0xD2800000u | ((uint32_t)(shift/16)<<21) | ((imm16&0xFFFF)<<5) | (uint32_t)rd;
}
/* MOVK Xd, #imm16, LSL #shift (keep other bits) */
static uint32_t a64_MOVK(int rd, unsigned imm16, int shift) {
    return 0xF2800000u | ((uint32_t)(shift/16)<<21) | ((imm16&0xFFFF)<<5) | (uint32_t)rd;
}
/* MOV Xd, Xm  (=ORR Xd, XZR, Xm) */
static uint32_t a64_MOV(int rd, int rm) {
    return 0xAA0003E0u | ((uint32_t)rm<<16) | (uint32_t)rd;
}
/* MOV Wd, Wm  (=ORR Wd, WZR, Wm) */
static uint32_t a64_MOV32(int rd, int rm) {
    return 0x2A0003E0u | ((uint32_t)rm<<16) | (uint32_t)rd;
}
/* ADRP Xd, #page_delta (signed 21-bit, in 4KB pages) */
static uint32_t a64_ADRP(int rd, int imm21) {
    uint32_t lo = (uint32_t)(imm21) & 3u;
    uint32_t hi = ((uint32_t)(imm21) >> 2) & 0x7FFFFu;
    return 0x90000000u | (lo<<29) | (hi<<5) | (uint32_t)rd;
}

/* --- Arithmetic (64-bit register form) --- */
static uint32_t a64_ADD(int rd, int rn, int rm) {
    return 0x8B000000u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_ADD_imm(int rd, int rn, int imm12) {
    return 0x91000000u | ((imm12&0xFFF)<<10) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_SUB(int rd, int rn, int rm) {
    return 0xCB000000u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_SUB_imm(int rd, int rn, int imm12) {
    return 0xD1000000u | ((imm12&0xFFF)<<10) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
/* --- Arithmetic (32-bit W-register form: same encoding, sf bit (31) cleared) --- */
static uint32_t a64_ADD32(int rd, int rn, int rm) {
    return 0x0B000000u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_ADD_imm32(int rd, int rn, int imm12) {
    return 0x11000000u | ((imm12&0xFFF)<<10) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_SUB32(int rd, int rn, int rm) {
    return 0x4B000000u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_SUB_imm32(int rd, int rn, int imm12) {
    return 0x51000000u | ((imm12&0xFFF)<<10) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
/* ADD/SUB (immediate) with the "LSL #12" shift option: represents imm12*4096.
 * Combined with the unshifted forms above, lets SP-relative frame adjustments
 * reach ~16MB in at most two instructions instead of truncating past 4095. */
static uint32_t a64_ADD_imm_sh12(int rd, int rn, int imm12) {
    return 0x91400000u | ((imm12&0xFFF)<<10) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_SUB_imm_sh12(int rd, int rn, int imm12) {
    return 0xD1400000u | ((imm12&0xFFF)<<10) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_MUL(int rd, int rn, int rm) {   /* MADD Xd, Xn, Xm, XZR */
    return 0x9B007C00u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_SDIV(int rd, int rn, int rm) {
    return 0x9AC00C00u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_UDIV(int rd, int rn, int rm) {
    return 0x9AC00800u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_NEG(int rd, int rm) {            /* SUB Xd, XZR, Xm */
    return 0xCB0003E0u | ((uint32_t)rm<<16) | (uint32_t)rd;
}

/* --- Bitwise --- */
static uint32_t a64_AND(int rd, int rn, int rm) {
    return 0x8A000000u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_ORR(int rd, int rn, int rm) {
    return 0xAA000000u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_EOR(int rd, int rn, int rm) {
    return 0xCA000000u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_AND32(int rd, int rn, int rm) {
    return 0x0A000000u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_ORR32(int rd, int rn, int rm) {
    return 0x2A000000u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_EOR32(int rd, int rn, int rm) {
    return 0x4A000000u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_MVN(int rd, int rm) {            /* ORN Xd, XZR, Xm */
    return 0xAA2003E0u | ((uint32_t)rm<<16) | (uint32_t)rd;
}
static uint32_t a64_LSL(int rd, int rn, int rm) {
    return 0x9AC02000u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_LSR(int rd, int rn, int rm) {
    return 0x9AC02400u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}
static uint32_t a64_ASR(int rd, int rn, int rm) {
    return 0x9AC02800u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rd;
}

/* --- Compare / test --- */
static uint32_t a64_CMP(int rn, int rm) {           /* SUBS XZR, Xn, Xm */
    return 0xEB00001Fu | ((uint32_t)rm<<16) | ((uint32_t)rn<<5);
}
static uint32_t a64_CMP_imm(int rn, int imm12) {    /* SUBS XZR, Xn, #imm */
    return 0xF100001Fu | ((imm12&0xFFF)<<10) | ((uint32_t)rn<<5);
}
static uint32_t a64_CMP32(int rn, int rm) {          /* SUBS WZR, Wn, Wm */
    return 0x6B00001Fu | ((uint32_t)rm<<16) | ((uint32_t)rn<<5);
}
static uint32_t a64_CMP_imm32(int rn, int imm12) {   /* SUBS WZR, Wn, #imm */
    return 0x7100001Fu | ((imm12&0xFFF)<<10) | ((uint32_t)rn<<5);
}
static uint32_t a64_TST(int rn, int rm) {            /* ANDS XZR, Xn, Xm */
    return 0xEA00001Fu | ((uint32_t)rm<<16) | ((uint32_t)rn<<5);
}
/* CSET Xd, cond  (=CSINC Xd, XZR, XZR, !cond) */
static uint32_t a64_CSET(int rd, int cond) {
    return 0x9A9F07E0u | ((uint32_t)(cond^1)<<12) | (uint32_t)rd;
}

/* --- Branches --- */
static uint32_t a64_Bcond(int cond, int imm19) {    /* B.cond #off (in instrs) */
    return 0x54000000u | ((uint32_t)(imm19)&0x7FFFFu)<<5 | (uint32_t)cond;
}
static uint32_t a64_B(int imm26) {
    return 0x14000000u | ((uint32_t)(imm26)&0x3FFFFFFu);
}
static uint32_t a64_BL(int imm26) {
    return 0x94000000u | ((uint32_t)(imm26)&0x3FFFFFFu);
}
static uint32_t a64_BLR(int rn) {
    return 0xD63F0000u | ((uint32_t)rn<<5);
}
static uint32_t a64_BR(int rn) {
    return 0xD61F0000u | ((uint32_t)rn<<5);
}
static uint32_t a64_RET(void) { return 0xD65F03C0u; }
static uint32_t a64_NOP(void) { return 0xD503201Fu; }

/* --- Load / store (64-bit GP, unsigned scaled offset) --- */
/* LDR Xt, [Xn, #off]  off must be 0..32760, multiple of 8 */
static uint32_t a64_LDR(int rt, int rn, int off) {
    return 0xF9400000u | (((uint32_t)(off/8)&0xFFFu)<<10) | ((uint32_t)rn<<5) | (uint32_t)rt;
}
/* STR Xt, [Xn, #off] */
static uint32_t a64_STR(int rt, int rn, int off) {
    return 0xF9000000u | (((uint32_t)(off/8)&0xFFFu)<<10) | ((uint32_t)rn<<5) | (uint32_t)rt;
}
/* LDUR Xt, [Xn, #simm9]  simm9 in -256..255 */
static uint32_t a64_LDUR(int rt, int rn, int simm9) {
    return 0xF8400000u | (((uint32_t)(simm9)&0x1FFu)<<12) | ((uint32_t)rn<<5) | (uint32_t)rt;
}
/* STUR Xt, [Xn, #simm9] */
static uint32_t a64_STUR(int rt, int rn, int simm9) {
    return 0xF8000000u | (((uint32_t)(simm9)&0x1FFu)<<12) | ((uint32_t)rn<<5) | (uint32_t)rt;
}
/* LDR Wt, [Xn, #off]  (32-bit, off must be multiple of 4) */
static uint32_t a64_LDR32(int rt, int rn, int off) {
    return 0xB9400000u | (((uint32_t)(off/4)&0xFFFu)<<10) | ((uint32_t)rn<<5) | (uint32_t)rt;
}
/* STR Wt, [Xn, #off] */
static uint32_t a64_STR32(int rt, int rn, int off) {
    return 0xB9000000u | (((uint32_t)(off/4)&0xFFFu)<<10) | ((uint32_t)rn<<5) | (uint32_t)rt;
}
/* LDRB Wt, [Xn, #off]  byte, zero-extend */
static uint32_t a64_LDRB(int rt, int rn, int off) {
    return 0x39400000u | (((uint32_t)(off)&0xFFFu)<<10) | ((uint32_t)rn<<5) | (uint32_t)rt;
}
/* STRB Wt, [Xn, #off] */
static uint32_t a64_STRB(int rt, int rn, int off) {
    return 0x39000000u | (((uint32_t)(off)&0xFFFu)<<10) | ((uint32_t)rn<<5) | (uint32_t)rt;
}
/* LDRSW Xt, [Xn, #off]  sign-extend 32→64, off multiple of 4 */
static uint32_t a64_LDRSW(int rt, int rn, int off) {
    return 0xB9800000u | (((uint32_t)(off/4)&0xFFFu)<<10) | ((uint32_t)rn<<5) | (uint32_t)rt;
}
/* LDRH Wt, [Xn, #off]  halfword zero-extend, off multiple of 2 */
static uint32_t a64_LDRH(int rt, int rn, int off) {
    return 0x79400000u | (((uint32_t)(off/2)&0xFFFu)<<10) | ((uint32_t)rn<<5) | (uint32_t)rt;
}
/* STRH Wt, [Xn, #off] */
static uint32_t a64_STRH(int rt, int rn, int off) {
    return 0x79000000u | (((uint32_t)(off/2)&0xFFFu)<<10) | ((uint32_t)rn<<5) | (uint32_t)rt;
}
/* LDR Xt, [Xn, Xm]  (register offset, no shift) */
static uint32_t a64_LDR_reg(int rt, int rn, int rm) {
    return 0xF8606800u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rt;
}
/* STR Xt, [Xn, Xm] */
static uint32_t a64_STR_reg(int rt, int rn, int rm) {
    return 0xF8206800u | ((uint32_t)rm<<16) | ((uint32_t)rn<<5) | (uint32_t)rt;
}

/* --- Store/load pair --- */
/* STP Xt1, Xt2, [Xn, #off]! (pre-index, off in -512..504, multiple of 8) */
static uint32_t a64_STP_pre(int t1, int t2, int rn, int off) {
    int imm7 = off/8;
    return 0xA9800000u | ((uint32_t)(imm7&0x7Fu)<<15) | ((uint32_t)t2<<10) | ((uint32_t)rn<<5) | (uint32_t)t1;
}
/* LDP Xt1, Xt2, [Xn], #off (post-index) */
static uint32_t a64_LDP_post(int t1, int t2, int rn, int off) {
    int imm7 = off/8;
    return 0xA8C00000u | ((uint32_t)(imm7&0x7Fu)<<15) | ((uint32_t)t2<<10) | ((uint32_t)rn<<5) | (uint32_t)t1;
}

/* --- Extension / sign extension --- */
/* SXTW Xd, Wn  (sign-extend 32→64) */
static uint32_t a64_SXTW(int rd, int rn) {
    return 0x93407C00u | ((uint32_t)rn<<5) | (uint32_t)rd;
}
/* UXTW Xd, Wn  (zero-extend 32→64, = AND Xd, Xn, #0xFFFFFFFF) */
static uint32_t a64_UXTW(int rd, int rn) {
    return 0xD3407C00u | ((uint32_t)rn<<5) | (uint32_t)rd;
}

/* --- Floating point (double precision, D registers) --- */
static uint32_t a64_FADD(int rd, int rn, int rm) { return 0x1E602800u|((uint32_t)rm<<16)|((uint32_t)rn<<5)|(uint32_t)rd; }
static uint32_t a64_FSUB(int rd, int rn, int rm) { return 0x1E603800u|((uint32_t)rm<<16)|((uint32_t)rn<<5)|(uint32_t)rd; }
static uint32_t a64_FMUL(int rd, int rn, int rm) { return 0x1E600800u|((uint32_t)rm<<16)|((uint32_t)rn<<5)|(uint32_t)rd; }
static uint32_t a64_FDIV(int rd, int rn, int rm) { return 0x1E601800u|((uint32_t)rm<<16)|((uint32_t)rn<<5)|(uint32_t)rd; }
static uint32_t a64_FNEG(int rd, int rm) { return 0x1E614000u|((uint32_t)rm<<5)|(uint32_t)rd; }
static uint32_t a64_FMOV(int rd, int rm) { return 0x1E604000u|((uint32_t)rm<<5)|(uint32_t)rd; }
static uint32_t a64_FCMP(int rn, int rm) { return 0x1E602000u|((uint32_t)rm<<16)|((uint32_t)rn<<5); }
/* SCVTF Dd, Xn  (int64 → double) */
static uint32_t a64_SCVTF(int rd, int rn) { return 0x9E620000u|((uint32_t)rn<<5)|(uint32_t)rd; }
/* FCVTZS Xd, Dn  (double → int64, truncate toward zero) */
static uint32_t a64_FCVTZS(int rd, int rn) { return 0x9E780000u|((uint32_t)rn<<5)|(uint32_t)rd; }
/* LDR Dd, [Xn, #off]  (double, off multiple of 8) */
static uint32_t a64_FLDR(int rt, int rn, int off) {
    return 0xFD400000u|(((uint32_t)(off/8)&0xFFFu)<<10)|((uint32_t)rn<<5)|(uint32_t)rt;
}
/* STR Dd, [Xn, #off] */
static uint32_t a64_FSTR(int rt, int rn, int off) {
    return 0xFD000000u|(((uint32_t)(off/8)&0xFFFu)<<10)|((uint32_t)rn<<5)|(uint32_t)rt;
}
/* LDR/STR Sd, [Xn, #off] (single precision, off multiple of 4) — this
 * backend represents every C float/double value as a 64-bit double in
 * registers and in most memory slots (a plain local var always gets a
 * conservative 8-byte-minimum stack slot, so that never mattered), but a
 * real C `float` struct field or array element is only 4 bytes and sits
 * tightly packed against its neighbors at the real ABI offset (e.g. a
 * Vulkan-style "struct { float x,y,r,g,b; }" vertex, spaced exactly 4
 * bytes apart to match what a real system Vulkan driver expects) — storing
 * one such field via the 8-byte a64_FSTR above overwrites the next field's
 * first 4 bytes too, and the field after that reads back corrupted
 * (reproduced directly: writing "v.a" then "v.b" on adjacent float fields
 * left v.a silently zeroed by v.b's oversized write). Callers narrow via
 * a64_FCVT_D_TO_S/widen via a64_FCVT_S_TO_D below when the real field/
 * element type is `float` rather than `double` — see a64_lvalue_size(). */
static uint32_t a64_FLDR_S(int rt, int rn, int off) {
    return 0xBD400000u|(((uint32_t)(off/4)&0xFFFu)<<10)|((uint32_t)rn<<5)|(uint32_t)rt;
}
static uint32_t a64_FSTR_S(int rt, int rn, int off) {
    return 0xBD000000u|(((uint32_t)(off/4)&0xFFFu)<<10)|((uint32_t)rn<<5)|(uint32_t)rt;
}
/* FCVT Sd, Dn (double -> single, narrowing) */
static uint32_t a64_FCVT_D_TO_S(int rd, int rn) { return 0x1E624000u|((uint32_t)rn<<5)|(uint32_t)rd; }
/* FCVT Dd, Sn (single -> double, widening) */
static uint32_t a64_FCVT_S_TO_D(int rd, int rn) { return 0x1E22C000u|((uint32_t)rn<<5)|(uint32_t)rd; }

/* CBZ Xt, #imm19  (compare and branch if zero) */
static uint32_t a64_CBZ(int rt, int imm19) {
    return 0xB4000000u | ((uint32_t)(imm19&0x7FFFF)<<5) | (uint32_t)rt;
}
/* CBNZ Xt, #imm19 */
static uint32_t a64_CBNZ(int rt, int imm19) {
    return 0xB5000000u | ((uint32_t)(imm19&0x7FFFF)<<5) | (uint32_t)rt;
}
/* CBZ/CBNZ Wt, #imm19 (32-bit forms: sf bit (31) cleared) */
static uint32_t a64_CBZ32(int rt, int imm19) {
    return 0x34000000u | ((uint32_t)(imm19&0x7FFFF)<<5) | (uint32_t)rt;
}
static uint32_t a64_CBNZ32(int rt, int imm19) {
    return 0x35000000u | ((uint32_t)(imm19&0x7FFFF)<<5) | (uint32_t)rt;
}

/* --- Exclusive load/store (LL/SC atomics) --- */
/* LDXR Xt, [Xn{,#0}] (64-bit) */
static uint32_t a64_LDXR(int rt, int rn) { return 0xC85F7C00u | ((uint32_t)rn<<5) | (uint32_t)rt; }
/* STXR Ws, Xt, [Xn{,#0}] (64-bit) — Ws=1 on failure, 0 on success */
static uint32_t a64_STXR(int rs, int rt, int rn) { return 0xC8007C00u | ((uint32_t)rs<<16) | ((uint32_t)rn<<5) | (uint32_t)rt; }
/* 32-bit (W-register) forms — same fields, size bits [31:30] = 10 instead
 * of 11. Needed for atomics on a plain "int" (4 bytes): using the 64-bit
 * form on a 4-byte C variable would read/write the 4 bytes past it too. */
static uint32_t a64_LDXR32(int rt, int rn) { return 0x885F7C00u | ((uint32_t)rn<<5) | (uint32_t)rt; }
static uint32_t a64_STXR32(int rs, int rt, int rn) { return 0x88007C00u | ((uint32_t)rs<<16) | ((uint32_t)rn<<5) | (uint32_t)rt; }
/* LDAXR/STLXR: acquire/release-ordered exclusive variants */
static uint32_t a64_LDAXR(int rt, int rn) { return 0xC85FFC00u | ((uint32_t)rn<<5) | (uint32_t)rt; }
static uint32_t a64_STLXR(int rs, int rt, int rn) { return 0xC800FC00u | ((uint32_t)rs<<16) | ((uint32_t)rn<<5) | (uint32_t)rt; }
/* CLREX — clear the local exclusive monitor */
static uint32_t a64_CLREX(void) { return 0xD5033F5Fu; }

/* --- Barriers --- */
static uint32_t a64_DMB_ISH(void) { return 0xD5033BBFu; }
static uint32_t a64_DMB_SY(void)  { return 0xD5033FBFu; }
static uint32_t a64_DSB_SY(void)  { return 0xD5033F9Fu; }
static uint32_t a64_ISB(void)     { return 0xD5033FDFu; }

/* --- Wait/signal for event (spinlock backoff) --- */
static uint32_t a64_WFE(void) { return 0xD503205Fu; }
static uint32_t a64_SEV(void) { return 0xD503209Fu; }

/* BRK #imm16 (debug breakpoint) */
static uint32_t a64_BRK(int imm16) { return 0xD4200000u | ((uint32_t)(imm16&0xFFFF)<<5); }

#endif /* ARM64_ASM_H */
