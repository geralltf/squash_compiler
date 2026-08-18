#include "assembler.h"
#include "symtable.h"
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* portable strdup replacement */
char* my_strdup(const char* src);

/* =========================================================================
 * Init / free
 * ========================================================================= */
void asm_init(Assembler *a, int is_64bit) {
    memset(a, 0, sizeof *a);
    a->is_64bit  = is_64bit;
    a->chkstk_movrax_off = -1;
    a->code_cap  = ASM_BUF_INIT;
    a->code      = malloc(a->code_cap);
    a->reloc_cap = 256;
    a->relocs    = malloc(a->reloc_cap * sizeof(Relocation));
    a->label_cap = 64;
    a->labels    = malloc(a->label_cap * sizeof(Label));
    a->fixup_cap = 256;
    a->fixups    = malloc(a->fixup_cap * sizeof(Fixup));
    /* Static data section */
    a->data_cap      = 4096;
    a->data_buf      = calloc(a->data_cap, 1);
    a->data_sym_cap  = 64;
    a->data_syms     = malloc(a->data_sym_cap * sizeof(DataSymbol));
}
void asm_free(Assembler *a) {
    free(a->code);
    for (int i=0;i<a->reloc_count;i++) free(a->relocs[i].symbol);
    free(a->relocs);
    for (int i=0;i<a->label_count;i++) free(a->labels[i].name);
    free(a->labels);
    for (int i=0;i<a->fixup_count;i++) ; /* no heap in fixup */
    free(a->fixups);
    free(a->data_buf);
    for (int i=0;i<a->data_sym_count;i++) free(a->data_syms[i].name);
    free(a->data_syms);
}

/* =========================================================================
 * Raw emission
 * ========================================================================= */
static void grow_code(Assembler *a, int need) {
    while (a->code_len + need > a->code_cap) {
        a->code_cap *= 2;
        a->code = realloc(a->code, a->code_cap);
    }
}
void asm_emit1(Assembler *a, uint8_t b) {
    grow_code(a,1); a->code[a->code_len++]=b;
}
void asm_emit2(Assembler *a, uint8_t b0, uint8_t b1) {
    grow_code(a,2); a->code[a->code_len++]=b0; a->code[a->code_len++]=b1;
}
void asm_emit3(Assembler *a, uint8_t b0, uint8_t b1, uint8_t b2) {
    grow_code(a,3);
    a->code[a->code_len++]=b0;
    a->code[a->code_len++]=b1;
    a->code[a->code_len++]=b2;
}
void asm_emit4(Assembler *a, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    grow_code(a,4);
    a->code[a->code_len++]=b0; a->code[a->code_len++]=b1;
    a->code[a->code_len++]=b2; a->code[a->code_len++]=b3;
}
void asm_emit_u16(Assembler *a, uint16_t v) {
    grow_code(a,2);
    a->code[a->code_len++]= v      &0xFF;
    a->code[a->code_len++]=(v>>8) &0xFF;
}
void asm_emit_u32(Assembler *a, uint32_t v) {
    grow_code(a,4);
    a->code[a->code_len++]= v       &0xFF;
    a->code[a->code_len++]=(v>>8)  &0xFF;
    a->code[a->code_len++]=(v>>16) &0xFF;
    a->code[a->code_len++]=(v>>24) &0xFF;
}
void asm_emit_u64(Assembler *a, uint64_t v) {
    for(int i=0;i<8;i++) { grow_code(a,1); a->code[a->code_len++]=(v>>(8*i))&0xFF; }
}
void asm_emit_bytes(Assembler *a, const uint8_t *buf, int len) {
    grow_code(a,len); memcpy(a->code+a->code_len,buf,len); a->code_len+=len;
}

/* =========================================================================
 * Labels
 * ========================================================================= */
int asm_new_label(Assembler *a, const char *name) {
    if (a->label_count == a->label_cap) {
        a->label_cap *= 2;
        a->labels = realloc(a->labels, a->label_cap*sizeof(Label));
    }
    int id = a->label_count++;
    a->labels[id].name   = my_strdup(name ? name : "");
    a->labels[id].offset = -1;
    return id;
}
void asm_def_label(Assembler *a, int id) {
    a->labels[id].offset = a->code_len;
}
void asm_emit_fixup(Assembler *a, int label_id) {
    if (a->fixup_count == a->fixup_cap) {
        a->fixup_cap *= 2;
        a->fixups = realloc(a->fixups, a->fixup_cap*sizeof(Fixup));
    }
    a->fixups[a->fixup_count].patch_offset = a->code_len;
    a->fixups[a->fixup_count].label_id     = label_id;
    a->fixup_count++;
    asm_emit_u32(a, 0); /* placeholder */
}
void asm_resolve(Assembler *a) {
    for (int i=0;i<a->fixup_count;i++) {
        int patch = a->fixups[i].patch_offset;
        int lid   = a->fixups[i].label_id;
        int target = a->labels[lid].offset;
        if (target < 0) {
            /* An internal codegen label with no matching definition — a
             * squash compiler bug (codegen emitted a jump/reference to a
             * label it never defined), not something attributable to any
             * particular line of the user's source. */
            diag_emit(DIAG_ERROR, -1, NULL, NULL,
                      "internal: undefined label %d (name=%s) — this is a squash bug, not an error in your source",
                      lid, (lid<a->label_count && a->labels[lid].name) ? a->labels[lid].name : "?");
            exit(1);
        }
        /* pc-relative: target - (patch + 4) */
        int32_t disp = (int32_t)(target - (patch + 4));
        a->code[patch+0] = (uint8_t)(disp);
        a->code[patch+1] = (uint8_t)(disp>>8);
        a->code[patch+2] = (uint8_t)(disp>>16);
        a->code[patch+3] = (uint8_t)(disp>>24);
    }
}

/* =========================================================================
 * Relocations
 * ========================================================================= */
static void add_reloc(Assembler *a, RelocKind kind, const char *sym, int addend) {
    if (a->reloc_count == a->reloc_cap) {
        a->reloc_cap *= 2;
        a->relocs = realloc(a->relocs, a->reloc_cap*sizeof(Relocation));
    }
    Relocation *r = &a->relocs[a->reloc_count++];
    r->offset  = a->code_len;
    r->kind    = kind;
    r->symbol  = my_strdup(sym);
    r->addend  = addend;
    asm_emit_u32(a, 0); /* placeholder */
}
void asm_reloc_iat (Assembler *a, const char *sym) {
    add_reloc(a, a->is_64bit ? RELOC_IAT_REL32 : RELOC_ABS32, sym, 0);
}
void asm_reloc_data(Assembler *a, const char *sym) {
    add_reloc(a, a->is_64bit ? RELOC_DATA_REL32 : RELOC_DATA_ABS32, sym, 0);
}

/* =========================================================================
 * REX prefix helpers (64-bit)
 * ========================================================================= */
static int needs_rex_r(Reg r) { return (int)r >= 8; }
static int reg_enc(Reg r)     { return (int)r & 7; }

/* Emit REX.W prefix for 64-bit operand size */
static void rex_w(Assembler *a, Reg reg, Reg rm) {
    uint8_t rex = 0x48;
    if (needs_rex_r(reg)) rex |= 0x44; /* REX.R */
    if (needs_rex_r(rm))  rex |= 0x41; /* REX.B */
    asm_emit1(a, rex);
}
/* Emit REX prefix for 64-bit reg-only operations */
static void rex_w_single(Assembler *a, Reg r) {
    uint8_t rex = 0x48;
    if (needs_rex_r(r)) rex |= 0x41;
    asm_emit1(a, rex);
}
/* ModRM byte: mod=3 (register), reg, rm */
static void modrm_rr(Assembler *a, Reg reg, Reg rm) {
    asm_emit1(a, 0xC0 | (reg_enc(reg)<<3) | reg_enc(rm));
}
/* ModRM for [base + disp8/32] */
static void modrm_disp(Assembler *a, Reg reg, Reg base, int disp) {
    if (disp == 0 && reg_enc(base) != 5) {
        asm_emit1(a, (reg_enc(reg)<<3) | reg_enc(base));
        if (reg_enc(base)==4) asm_emit1(a,0x24); /* SIB for RSP */
    } else if (disp >= -128 && disp <= 127) {
        asm_emit1(a, 0x40 | (reg_enc(reg)<<3) | reg_enc(base));
        if (reg_enc(base)==4) asm_emit1(a,0x24);
        asm_emit1(a, (uint8_t)(int8_t)disp);
    } else {
        asm_emit1(a, 0x80 | (reg_enc(reg)<<3) | reg_enc(base));
        if (reg_enc(base)==4) asm_emit1(a,0x24);
        asm_emit_u32(a, (uint32_t)(int32_t)disp);
    }
}

/* =========================================================================
 * Stack frame
 * ========================================================================= */
void asm_push_reg(Assembler *a, Reg r) {
    if (a->is_64bit) {
        if (needs_rex_r(r)) asm_emit1(a, 0x41);
        asm_emit1(a, 0x50 | reg_enc(r));
    } else {
        asm_emit1(a, 0x50 | (r&7));
    }
}
void asm_pop_reg(Assembler *a, Reg r) {
    if (a->is_64bit) {
        if (needs_rex_r(r)) asm_emit1(a, 0x41);
        asm_emit1(a, 0x58 | reg_enc(r));
    } else {
        asm_emit1(a, 0x58 | (r&7));
    }
}
void asm_push_imm32(Assembler *a, int32_t v) {
    /* Windows handle constants: use xor/sub/push instead of push imm8/32 */
    if (v >= -12 && v <= -10) {
        asm_emit2(a, 0x31, 0xC0);               /* xor eax,eax           */
        asm_emit3(a, 0x83, 0xE8, (uint8_t)(-v));/* sub eax,|v|           */
        asm_emit1(a, 0x50);                      /* push eax              */
        return;
    }
    if (v>=-128 && v<=127) { asm_emit1(a,0x6A); asm_emit1(a,(uint8_t)(int8_t)v); }
    else { asm_emit1(a,0x68); asm_emit_u32(a,(uint32_t)v); }
}
void asm_sub_rsp(Assembler *a, int32_t n) {
    if (n==0) return;
    if (a->is_64bit) {
        if (n>=-128&&n<=127) { asm_emit3(a,0x48,0x83,0xEC); asm_emit1(a,(uint8_t)(int8_t)n); }
        else { asm_emit2(a,0x48,0x81); asm_emit1(a,0xEC); asm_emit_u32(a,(uint32_t)n); }
    } else {
        if (n>=-128&&n<=127) { asm_emit2(a,0x83,0xEC); asm_emit1(a,(uint8_t)(int8_t)n); }
        else { asm_emit1(a,0x81); asm_emit1(a,0xEC); asm_emit_u32(a,(uint32_t)n); }
    }
}
void asm_add_rsp(Assembler *a, int32_t n) {
    if (n==0) return;
    if (a->is_64bit) {
        if (n>=-128&&n<=127) { asm_emit3(a,0x48,0x83,0xC4); asm_emit1(a,(uint8_t)(int8_t)n); }
        else { asm_emit2(a,0x48,0x81); asm_emit1(a,0xC4); asm_emit_u32(a,(uint32_t)n); }
    } else {
        if (n>=-128&&n<=127) { asm_emit2(a,0x83,0xC4); asm_emit1(a,(uint8_t)(int8_t)n); }
        else { asm_emit1(a,0x81); asm_emit1(a,0xC4); asm_emit_u32(a,(uint32_t)n); }
    }
}
void asm_enter(Assembler *a, int local_bytes) {
    asm_push_reg(a, REG_RBP);
    if (a->is_64bit) {
        asm_emit3(a,0x48,0x89,0xE5); /* mov rbp, rsp */
    } else {
        asm_emit2(a,0x89,0xE5);      /* mov ebp, esp */
    }
    /* align locals to 16 bytes */
    int aligned = (local_bytes + 15) & ~15;
    if (a->is_64bit && aligned > 0) {
        /* extra 8 to keep RSP aligned after push RBP */
        if ((aligned & 8) == 0) aligned += 8;
    }
    asm_sub_rsp(a, aligned);
}

/* Emit prologue with a patchable frame size.
 * chkstk_lbl: label ID of our __chkstk helper (-1 to skip probing).
 * For 64-bit with chkstk_lbl >= 0, emits:
 *   push rbp / mov rbp,rsp / mov eax,0 / call __chkstk / sub rsp,0
 * For 32-bit or chkstk_lbl < 0, emits:
 *   push rbp / mov rbp,rsp / sub rsp,0
 * Returns offset of the sub rsp/esp imm32 placeholder for asm_patch_frame. */
int asm_enter_deferred(Assembler *a, int chkstk_lbl) {
    a->chkstk_movrax_off = -1;
    asm_push_reg(a, REG_RBP);
    if (a->is_64bit) {
        asm_emit3(a, 0x48,0x89,0xE5);       /* mov rbp, rsp           */
        /* Windows x64 ABI requires a callee to preserve RBX/RSI/RDI/
         * R12-R15 across a call — squash's own codegen uses RBX freely as
         * scratch throughout every function body (the push/pop-based
         * "compute a value" idiom used pervasively in codegen.c), which
         * is invisible for squash-to-squash calls (both sides agree on
         * the same convention) and for squash calling OUT to a real DLL
         * (the DLL follows the real ABI and preserves whatever it uses).
         * The one place this actually breaks is the reverse direction:
         * a squash-compiled function used as a callback that the OS
         * calls INTO directly — e.g. a window procedure registered via
         * RegisterClassExA's lpfnWndProc, invoked by DispatchMessageA.
         * Clobbering RBX there corrupts the caller's (Windows' own)
         * register state, surfacing as a seemingly random crash further
         * up the call stack sometime after the callback returns — not
         * inside the callback itself, which is why this went undetected
         * until a real WM_SIZE/WM_ACTIVATE message actually got
         * dispatched through a squash-compiled WNDPROC. Pushed here
         * (8 registers incl. one placeholder for 16-byte-multiple
         * padding, so this project's established "RSP stays 8-mod-16
         * throughout a function body" convention — see AST_CALL's own
         * frame-size comments — is unaffected) and popped by
         * emit_win64_callee_restore() before every real return. Linux
         * (SysV) is untouched: its callee-saved set differs (no RSI/RDI)
         * and this bug is Windows-callback-specific. */
        if (!a->is_linux) {
            asm_push_reg(a, REG_RBX);
            asm_push_reg(a, REG_RSI);
            asm_push_reg(a, REG_RDI);
            asm_push_reg(a, REG_R12);
            asm_push_reg(a, REG_R13);
            asm_push_reg(a, REG_R14);
            asm_push_reg(a, REG_R15);
            asm_sub_rsp(a, 8); /* padding: keep the 16-byte-multiple parity intact */
        } else {
            /* SysV (real Linux AND macOS, both is_linux here) ALSO requires
             * a callee to preserve RBX/R12-R15 across a call (RSI/RDI are
             * argument/caller-saved on SysV, unlike Windows, so they're not
             * needed here) -- see SQ_SYSV_CALLEE_SAVE_WORDS's own comment
             * in symtable.h for the full story and how this was found (a
             * real qsort() comparator callback, no SDL3 involved at all,
             * reliably corrupted its own first parameter on every run).
             * SQ_SYSV_CALLEE_SAVE_WORDS (6 words: 5 registers + 1 padding
             * slot for the same 16-byte-parity reasoning as the Win64
             * branch above) must match symtable_define_param's/codegen.c's
             * reserved offset exactly, or parameter/local storage would
             * overlap this pushed register area. */
            asm_push_reg(a, REG_RBX);
            asm_push_reg(a, REG_R12);
            asm_push_reg(a, REG_R13);
            asm_push_reg(a, REG_R14);
            asm_push_reg(a, REG_R15);
            asm_sub_rsp(a, 8); /* padding: keep the 16-byte-multiple parity intact */
        }
        if (chkstk_lbl >= 0) {
            asm_emit1(a, 0xB8);             /* mov eax, imm32 (opcode) */
            a->chkstk_movrax_off = a->code_len;
            asm_emit_u32(a, 0);             /* placeholder = 0        */
            asm_call_direct(a, chkstk_lbl); /* call __chkstk_probe    */
        }
        asm_emit3(a, 0x48,0x81,0xEC);       /* sub rsp, imm32         */
    } else {
        asm_emit2(a, 0x89,0xE5);            /* mov ebp, esp           */
        if (chkstk_lbl >= 0) {
            asm_emit1(a, 0xB8);             /* mov eax, imm32 (opcode) */
            a->chkstk_movrax_off = a->code_len;
            asm_emit_u32(a, 0);             /* placeholder = 0        */
            asm_call_direct(a, chkstk_lbl); /* call __chkstk_probe32  */
        }
        asm_emit2(a, 0x81,0xEC);            /* sub esp, imm32         */
    }
    int patch_off = a->code_len;            /* offset of the imm32    */
    asm_emit_u32(a, 0);                     /* placeholder = 0        */
    return patch_off;
}

/* Patch the frame size emitted by asm_enter_deferred.
 * aligned_size must already be 16-byte aligned and RSP-adjusted.
 * Also patches the mov eax imm32 for __chkstk if it was emitted.   */
void asm_patch_frame(Assembler *a, int patch_off, int aligned_size) {
    if (patch_off >= 0 && patch_off + 4 <= a->code_len) {
        a->code[patch_off+0] = (uint8_t)(aligned_size);
        a->code[patch_off+1] = (uint8_t)(aligned_size>>8);
        a->code[patch_off+2] = (uint8_t)(aligned_size>>16);
        a->code[patch_off+3] = (uint8_t)(aligned_size>>24);
    }
    /* Patch the mov eax,N placeholder for __chkstk (if emitted) */
    int mo = a->chkstk_movrax_off;
    if (mo >= 0 && mo + 4 <= a->code_len) {
        a->code[mo+0] = (uint8_t)(aligned_size);
        a->code[mo+1] = (uint8_t)(aligned_size>>8);
        a->code[mo+2] = (uint8_t)(aligned_size>>16);
        a->code[mo+3] = (uint8_t)(aligned_size>>24);
    }
    a->chkstk_movrax_off = -1; /* reset for next function */
}
void asm_leave(Assembler *a) { asm_emit1(a, 0xC9); }
void asm_ret  (Assembler *a) { asm_emit1(a, 0xC3); }

/* Restores the Windows x64 callee-saved registers saved by
 * asm_enter_deferred (see its own comment for why). Must run BEFORE every
 * asm_leave()+asm_ret() pair.
 *
 * Reads via FIXED RBP-relative loads (mov reg,[rbp-N]), NOT stack pops —
 * this bit for real the first time it was exercised by a genuine loop with
 * a live local variable (SDL3's PRIVATE_PumpEvents, a MSG-sized local plus
 * a PeekMessageA/TranslateMessage/DispatchMessageA loop calling back into a
 * squash-compiled WNDPROC): this codebase's calling convention frees local
 * variables ONLY via "leave" (mov rsp,rbp) at the very end, never via an
 * explicit "add rsp,N" mid-function — so at the point this restore code
 * runs, RSP still points BELOW the live locals frame, not at the top of
 * the 7 registers pushed in the prologue. A `pop` sequence here read
 * whatever bytes happened to sit at the CURRENT (wrong) RSP — i.e. bytes
 * belonging to the function's own locals — instead of the saved register
 * values, silently handing back garbage to the caller in RBX/RSI/RDI/
 * R12-R15 while still "successfully" returning to the right address (since
 * `leave` recovers RSP from RBP unconditionally, independent of whatever
 * RSP drifted to). This is exactly why the symptom was "the callee's own
 * body runs to completion correctly, and control returns to the right
 * place, but something goes wrong sometime after" — the corruption is in
 * the REGISTER VALUES the caller gets back, not in the control flow.
 * RBP-relative loads sidestep this entirely: these 7 registers were pushed
 * immediately after "push rbp; mov rbp,rsp" (before any locals or per-call
 * frames existed), so they always live at the same fixed offsets below
 * RBP — [rbp-8]=RBX .. [rbp-56]=R15 (see asm_enter_deferred's own push
 * order) — regardless of whatever the rest of the function did to RSP.
 * No-op on 32-bit. On SysV (real Linux and macOS), restores the smaller
 * RBX/R12-R15 set asm_enter_deferred's "else" branch pushes instead (see
 * SQ_SYSV_CALLEE_SAVE_WORDS's own comment in symtable.h) -- same
 * RBP-relative-load reasoning as the Win64 case above applies identically
 * here, just with 5 registers instead of 7. */
void asm_win64_callee_restore(Assembler *a) {
    if (!a->is_64bit) {
        return;
    }
    if (!a->is_linux) {
        asm_mov_reg_mem(a, REG_RBX, REG_RBP, -8);
        asm_mov_reg_mem(a, REG_RSI, REG_RBP, -16);
        asm_mov_reg_mem(a, REG_RDI, REG_RBP, -24);
        asm_mov_reg_mem(a, REG_R12, REG_RBP, -32);
        asm_mov_reg_mem(a, REG_R13, REG_RBP, -40);
        asm_mov_reg_mem(a, REG_R14, REG_RBP, -48);
        asm_mov_reg_mem(a, REG_R15, REG_RBP, -56);
    } else {
        asm_mov_reg_mem(a, REG_RBX, REG_RBP, -8);
        asm_mov_reg_mem(a, REG_R12, REG_RBP, -16);
        asm_mov_reg_mem(a, REG_R13, REG_RBP, -24);
        asm_mov_reg_mem(a, REG_R14, REG_RBP, -32);
        asm_mov_reg_mem(a, REG_R15, REG_RBP, -40);
    }
}

/* =========================================================================
 * Data movement
 * ========================================================================= */
void asm_mov_reg_imm(Assembler *a, Reg dst, long long imm) {
    if (a->is_64bit) {
        if (imm >= 0 && imm <= 2147483647LL) {
            /* Small non-negative: mov r32, imm32  (zero-extends to r64 — safe
             * because upper 32 bits are zeroed and value is non-negative)    */
            if (needs_rex_r(dst)) asm_emit1(a, 0x41);
            asm_emit1(a, 0xB8 | reg_enc(dst));
            asm_emit_u32(a, (uint32_t)imm);
        } else if (imm >= -2147483648LL && imm < 0) {
            /* Negative value that fits in int32: use REX.W + C7 /0 imm32
             * which SIGN-EXTENDS the immediate into the full 64-bit register.
             * "mov rax, imm32(signed)" = 48 C7 C0 imm32                      */
            uint8_t rex = 0x48;
            if (needs_rex_r(dst)) rex |= 0x01; /* REX.B for extended regs    */
            asm_emit1(a, rex);
            asm_emit1(a, 0xC7);
            asm_emit1(a, 0xC0 | reg_enc(dst)); /* ModRM: mod=11, /0, rm=dst  */
            asm_emit_u32(a, (uint32_t)(int32_t)imm);
        } else {
            /* Full 64-bit immediate: movabs r64, imm64                        */
            rex_w_single(a, dst);
            asm_emit1(a, 0xB8 | reg_enc(dst));
            asm_emit_u64(a, (uint64_t)imm);
        }
    } else {
        asm_emit1(a, 0xB8 | reg_enc(dst));
        asm_emit_u32(a, (uint32_t)(int32_t)imm);
    }
}
void asm_mov_reg_reg(Assembler *a, Reg dst, Reg src) {
    if (a->is_64bit) {
        rex_w(a, src, dst);
        asm_emit1(a,0x89);
        modrm_rr(a, src, dst);
    } else {
        asm_emit1(a,0x89);
        asm_emit1(a, 0xC0|(reg_enc(src)<<3)|reg_enc(dst));
    }
}
void asm_mov_mem32_reg(Assembler *a, Reg base, int disp, Reg src) {
    /* MOV [base+disp], src -- 32-bit store (no REX.W), zero-extends nothing */
    if (needs_rex_r(src) || needs_rex_r(base)) {
        uint8_t rex = 0x40;
        if (needs_rex_r(src))  rex |= 0x04;
        if (needs_rex_r(base)) rex |= 0x01;
        asm_emit1(a, rex);
    }
    asm_emit1(a, 0x89);
    modrm_disp(a, src, base, disp);
}

void asm_mov_mem8_reg(Assembler *a, Reg base, int disp, Reg src) {
    /* MOV byte[base+disp], src_low8 */
    if (needs_rex_r(src) || needs_rex_r(base) || (int)src >= 4) {
        uint8_t rex = 0x40;
        if (needs_rex_r(src))  rex |= 0x04;
        if (needs_rex_r(base)) rex |= 0x01;
        asm_emit1(a, rex);
    }
    asm_emit1(a, 0x88);
    modrm_disp(a, src, base, disp);
}

void asm_mov_mem_reg(Assembler *a, Reg base, int disp, Reg src) {
    /* [base+disp] = src */
    if (a->is_64bit) {
        rex_w(a, src, base);
        asm_emit1(a,0x89);
        modrm_disp(a, src, base, disp);
    } else {
        asm_emit1(a,0x89);
        /* 32-bit disp encoding */
        if (disp==0) {
            asm_emit1(a,(reg_enc(src)<<3)|reg_enc(base));
            if (reg_enc(base)==4) asm_emit1(a,0x24);
        } else if (disp>=-128&&disp<=127) {
            asm_emit1(a,0x40|(reg_enc(src)<<3)|reg_enc(base));
            if (reg_enc(base)==4) asm_emit1(a,0x24);
            asm_emit1(a,(uint8_t)(int8_t)disp);
        } else {
            asm_emit1(a,0x80|(reg_enc(src)<<3)|reg_enc(base));
            if (reg_enc(base)==4) asm_emit1(a,0x24);
            asm_emit_u32(a,(uint32_t)(int32_t)disp);
        }
    }
}
void asm_mov_reg32_mem(Assembler *a, Reg dst, Reg base, int disp) {
    /* dst = [base+disp] — 32-bit load, zero-extends upper 32 bits of 64-bit reg */
    /* No REX.W prefix => 32-bit operand size, upper 32 bits of dst zeroed */
    if (needs_rex_r(dst) || needs_rex_r(base)) {
        uint8_t rex=0x40;
        if (needs_rex_r(dst)) rex|=0x04;
        if (needs_rex_r(base)) rex|=0x01;
        asm_emit1(a,rex);
    }
    asm_emit1(a,0x8B);
    modrm_disp(a, dst, base, disp);
}

void asm_mov_eax_mem8(Assembler *a, Reg base, int disp) {
    /* MOVZX EAX, byte[base+disp] — 8-bit load, zero-extends */
    asm_emit2(a, 0x0F, 0xB6);
    modrm_disp(a, REG_RAX, base, disp);
}

void asm_movsx_rax_mem8(Assembler *a, Reg base, int disp) {
    /* MOVSX RAX, byte[base+disp] — sign-extend 8-bit to 64-bit RAX */
    rex_w(a, REG_RAX, base);
    asm_emit1(a, 0x0F);
    asm_emit1(a, 0xBE); /* MOVSX r64, r/m8 */
    modrm_disp(a, REG_RAX, base, disp);
}

void asm_movsx_eax_mem8(Assembler *a, Reg base, int disp) {
    /* MOVSX EAX, byte[base+disp] — sign-extend 8-bit to 32-bit EAX (no REX prefix) */
    asm_emit2(a, 0x0F, 0xBE); /* MOVSX r32, r/m8 */
    modrm_disp(a, REG_EAX, base, disp);
}

void asm_movzx_eax_mem8(Assembler *a, Reg base, int disp) {
    /* MOVZX EAX, byte[base+disp] — zero-extend 8-bit to 32-bit EAX (no REX prefix) */
    asm_emit2(a, 0x0F, 0xB6); /* MOVZX r32, r/m8 */
    modrm_disp(a, REG_EAX, base, disp);
}

void asm_movsxd_rax_mem(Assembler *a, Reg base, int disp) {
    /* MOVSXD RAX, dword[base+disp] — sign-extend 32-bit to 64-bit RAX */
    rex_w(a, REG_RAX, base);
    asm_emit1(a, 0x63); /* MOVSXD r64, r/m32 */
    modrm_disp(a, REG_RAX, base, disp);
}

void asm_mov_reg_mem(Assembler *a, Reg dst, Reg base, int disp) {
    /* dst = [base+disp] */
    if (a->is_64bit) {
        rex_w(a, dst, base);
        asm_emit1(a,0x8B);
        modrm_disp(a, dst, base, disp);
    } else {
        asm_emit1(a,0x8B);
        if (disp==0) {
            asm_emit1(a,(reg_enc(dst)<<3)|reg_enc(base));
            if (reg_enc(base)==4) asm_emit1(a,0x24);
        } else if (disp>=-128&&disp<=127) {
            asm_emit1(a,0x40|(reg_enc(dst)<<3)|reg_enc(base));
            if (reg_enc(base)==4) asm_emit1(a,0x24);
            asm_emit1(a,(uint8_t)(int8_t)disp);
        } else {
            asm_emit1(a,0x80|(reg_enc(dst)<<3)|reg_enc(base));
            if (reg_enc(base)==4) asm_emit1(a,0x24);
            asm_emit_u32(a,(uint32_t)(int32_t)disp);
        }
    }
}

/* lea dst, [rip + data_symbol]  (64-bit only: 48 8D 05 <rel32>) */
void asm_lea_rip_data(Assembler *a, Reg dst, const char *sym) {
    if (!a->is_64bit) { printf("asm_lea_rip_data: 32-bit not supported\n"); exit(1); }
    rex_w(a, dst, REG_RAX);
    asm_emit1(a, 0x8D);
    /* ModRM: mod=00, reg=dst, rm=101 (RIP-relative) */
    asm_emit1(a, (reg_enc(dst)<<3)|0x05);
    asm_reloc_data(a, sym);  /* emits 4-byte placeholder + records reloc */
}

/* lea dst, [rip + sym]  (64-bit only: 48 8D 05 <rel32>), resolved via
 * RELOC_STATIC_REL32 — the SAME cross-object relocation asm_call_static()
 * uses for a "call rel32" to a sibling-object function, just emitted after
 * a LEA opcode instead of a CALL. Valid because x64 RIP-relative addressing
 * for both CALL and LEA computes the same displacement (target minus the
 * address of the NEXT instruction, i.e. reloc-site+4 in both encodings —
 * see objfile.c's merge-time patcher, which doesn't care which opcode
 * preceded the 4-byte immediate). Needed for taking the ADDRESS of (not
 * calling) a function that's declared-but-not-yet-defined in this object —
 * e.g. passing a cross-object function's name as a callback argument, as
 * real SDL3 code does throughout (SDL_CreateHashTable(..., SDL_HashID,
 * SDL_KeyMatchID, SDL_DestroyHashValue, ...) in a video/audio-part object,
 * where those three callbacks are actually defined in the core part). */
void asm_lea_rip_static(Assembler *a, Reg dst, const char *sym) {
    if (!a->is_64bit) { printf("asm_lea_rip_static: 32-bit not supported\n"); exit(1); }
    rex_w(a, dst, REG_RAX);
    asm_emit1(a, 0x8D);
    asm_emit1(a, (reg_enc(dst)<<3)|0x05);
    add_reloc(a, RELOC_STATIC_REL32, sym, 0);
}

/* lea dst, [rbp + disp] */
void asm_lea_rbp_disp(Assembler *a, Reg dst, int disp) {
    if (a->is_64bit) {
        rex_w(a, dst, REG_RBP);
        asm_emit1(a,0x8D);
        modrm_disp(a, dst, REG_RBP, disp);
    } else {
        asm_emit1(a,0x8D);
        if (disp>=-128&&disp<=127) {
            asm_emit1(a,0x45|(reg_enc(dst)<<3));
            asm_emit1(a,(uint8_t)(int8_t)disp);
        } else {
            asm_emit1(a,0x85|(reg_enc(dst)<<3));
            asm_emit_u32(a,(uint32_t)(int32_t)disp);
        }
    }
}

/* =========================================================================
 * Arithmetic
 * ========================================================================= */
void asm_add_reg_reg(Assembler *a, Reg dst, Reg src) {
    if (a->is_64bit) { rex_w(a,src,dst); asm_emit1(a,0x01); modrm_rr(a,src,dst); }
    else { asm_emit1(a,0x01); asm_emit1(a,0xC0|(reg_enc(src)<<3)|reg_enc(dst)); }
}
void asm_sub_reg_reg(Assembler *a, Reg dst, Reg src) {
    if (a->is_64bit) { rex_w(a,src,dst); asm_emit1(a,0x29); modrm_rr(a,src,dst); }
    else { asm_emit1(a,0x29); asm_emit1(a,0xC0|(reg_enc(src)<<3)|reg_enc(dst)); }
}
void asm_imul_reg_reg(Assembler *a, Reg dst, Reg src) {
    if (a->is_64bit) { rex_w(a,dst,src); asm_emit2(a,0x0F,0xAF); modrm_rr(a,dst,src); }
    else { asm_emit2(a,0x0F,0xAF); asm_emit1(a,0xC0|(reg_enc(dst)<<3)|reg_enc(src)); }
}
void asm_idiv_reg(Assembler *a, Reg src) {
    /* cdq / cqo then idiv */
    if (a->is_64bit) { asm_emit2(a,0x48,0x99); rex_w_single(a,src); asm_emit1(a,0xF7); asm_emit1(a,0xF8|reg_enc(src)); }
    else { asm_emit1(a,0x99); asm_emit1(a,0xF7); asm_emit1(a,0xF8|reg_enc(src)); }
}
void asm_div_reg(Assembler *a, Reg src) {
    /* Unsigned counterpart to asm_idiv_reg(): "xor edx,edx" (zeroing EDX
     * also zero-extends the upper 32 bits of RDX in 64-bit mode, unlike
     * cqo/cdq's SIGN-extension) then "div src" (opcode 0xF7 /6, vs idiv's
     * /7 — the ModRM reg-field extension bits 0xF0 vs 0xF8). Needed for
     * unsigned 64-bit division/modulo: using idiv (signed) on an unsigned
     * value with the high bit set — e.g. 0xFFFFFFFFFFFFFFFF, "-1" if
     * misread as signed — sign-extends it to a negative RDX:RAX pair and
     * computes a signed quotient/remainder instead of the correct unsigned
     * one. Confirmed via isolated repro: unsigned "0xFFFFFFFFFFFFFFFFULL %
     * 10" returned 0xFFFFFFFFFFFFFFFF (i.e. -1 unchanged, matching signed
     * "-1 % 10 == -1") and "/ 10" returned 0 (matching signed "-1 / 10 ==
     * 0"), instead of the correct unsigned answers (5 and
     * 1844674407370955161) — this broke SDL_ulltoa()/SDL_string.c's own
     * printf-family %llu/%zu formatting for any value >= 2^63. */
    asm_emit2(a,0x31,0xD2); /* xor edx,edx */
    if (a->is_64bit) { rex_w_single(a,src); asm_emit1(a,0xF7); asm_emit1(a,0xF0|reg_enc(src)); }
    else { asm_emit1(a,0xF7); asm_emit1(a,0xF0|reg_enc(src)); }
}
void asm_neg_reg(Assembler *a, Reg r) {
    /* Use IMUL r,r,-1 (equivalent to NEG) to avoid AV heuristics on NEG/NOT+INC */
    /* 64-bit: REX.W 6B /r FF  e.g. 48 6B C0 FF = IMUL RAX,RAX,-1 */
    /* 32-bit:         6B /r FF  e.g.    6B C0 FF = IMUL EAX,EAX,-1 */
    int enc = reg_enc(r);
    int modrm = 0xC0 | (enc<<3) | enc;  /* mod=11, reg=r, rm=r */
    if (a->is_64bit) { rex_w_single(a,r); }
    asm_emit1(a,0x6B); asm_emit1(a,(uint8_t)modrm); asm_emit1(a,0xFF);
}
void asm_not_reg(Assembler *a, Reg r) {
    if (a->is_64bit) { rex_w_single(a,r); asm_emit1(a,0xF7); asm_emit1(a,0xD0|reg_enc(r)); }
    else { asm_emit1(a,0xF7); asm_emit1(a,0xD0|reg_enc(r)); }
}

/* =========================================================================
 * Bitwise
 * ========================================================================= */
void asm_and_reg_reg(Assembler *a, Reg dst, Reg src) {
    if (a->is_64bit) { rex_w(a,src,dst); asm_emit1(a,0x21); modrm_rr(a,src,dst); }
    else { asm_emit1(a,0x21); asm_emit1(a,0xC0|(reg_enc(src)<<3)|reg_enc(dst)); }
}
void asm_or_reg_reg(Assembler *a, Reg dst, Reg src) {
    if (a->is_64bit) { rex_w(a,src,dst); asm_emit1(a,0x09); modrm_rr(a,src,dst); }
    else { asm_emit1(a,0x09); asm_emit1(a,0xC0|(reg_enc(src)<<3)|reg_enc(dst)); }
}
void asm_xor_reg_reg(Assembler *a, Reg dst, Reg src) {
    if (a->is_64bit) { rex_w(a,src,dst); asm_emit1(a,0x31); modrm_rr(a,src,dst); }
    else { asm_emit1(a,0x31); asm_emit1(a,0xC0|(reg_enc(src)<<3)|reg_enc(dst)); }
}
void asm_shl_reg_cl(Assembler *a, Reg r) {
    if (a->is_64bit) { rex_w_single(a,r); asm_emit1(a,0xD3); asm_emit1(a,0xE0|reg_enc(r)); }
    else { asm_emit1(a,0xD3); asm_emit1(a,0xE0|reg_enc(r)); }
}
void asm_shr_reg_cl(Assembler *a, Reg r) {
    if (a->is_64bit) { rex_w_single(a,r); asm_emit1(a,0xD3); asm_emit1(a,0xE8|reg_enc(r)); }
    else { asm_emit1(a,0xD3); asm_emit1(a,0xE8|reg_enc(r)); }
}

/* =========================================================================
 * Comparison & logical
 * ========================================================================= */
void asm_cmp_reg_reg(Assembler *a, Reg a_, Reg b) {
    if (a->is_64bit) { rex_w(a,b,a_); asm_emit1(a,0x39); modrm_rr(a,b,a_); }
    else { asm_emit1(a,0x39); asm_emit1(a,0xC0|(reg_enc(b)<<3)|reg_enc(a_)); }
}
void asm_test_reg_reg(Assembler *a, Reg a_, Reg b) {
    if (a->is_64bit) { rex_w(a,b,a_); asm_emit1(a,0x85); modrm_rr(a,b,a_); }
    else { asm_emit1(a,0x85); asm_emit1(a,0xC0|(reg_enc(b)<<3)|reg_enc(a_)); }
}
void asm_setcc_al(Assembler *a, CondCode cc) {
    /* SETCC AL — must be called immediately after CMP/TEST while flags are valid */
    asm_emit3(a,0x0F, 0x90|(uint8_t)cc, 0xC0); /* SETCC AL */
}
void asm_movzx_rax_al(Assembler *a) {
    /* MOVZX EAX,AL — zero-extends AL to EAX (clears upper 32 bits of RAX) without modifying flags */
    asm_emit3(a, 0x0F, 0xB6, 0xC0); /* movzx eax, al */
}

/* =========================================================================
 * Control flow
 * ========================================================================= */
void asm_jmp_label(Assembler *a, int label_id) {
    asm_emit1(a,0xE9); asm_emit_fixup(a,label_id);
}
void asm_call_label(Assembler *a, int label_id) {
    asm_emit1(a,0xE8); asm_emit_fixup(a,label_id); /* CALL rel32 */
}
void asm_jcc_label(Assembler *a, CondCode cc, int label_id) {
    asm_emit2(a,0x0F, 0x80|(uint8_t)cc); asm_emit_fixup(a,label_id);
}

/* call qword ptr [RIP + rel32]  -- 64-bit import call via IAT */
void asm_call_import(Assembler *a, const char *sym) {
    asm_emit2(a,0xFF,0x15);
    asm_reloc_iat(a, sym);
}

/* mov dst, qword ptr [RIP + rel32]  -- load the IAT slot's CONTENT (the
 * real imported function's address, filled in by the Windows loader) as a
 * plain value, instead of calling through it. Needed when a DLL-imported
 * function's NAME is referenced without being called directly — e.g.
 * passed as a function-pointer argument ("_beginthreadex" handed to
 * SDL_CreateThreadRuntime via SDL3's SDL_BeginThreadFunction macro). Same
 * REX/ModRM shape as asm_lea_rip_wdata, but MOV (0x8B) reads the pointer
 * AT that address rather than LEA (0x8D) computing the address of the
 * relocation site itself. */
void asm_load_import_addr(Assembler *a, Reg dst, const char *sym) {
    if (!a->is_64bit) {
        /* 32-bit: mov dst, [abs32] IAT slot */
        uint8_t modrm = 0x05 | ((uint8_t)reg_enc(dst) << 3);
        asm_emit2(a, 0x8B, modrm);
        asm_reloc_iat(a, sym);
        return;
    }
    uint8_t rex = 0x48;
    int enc = reg_enc(dst);
    if (needs_rex_r(dst)) rex |= 0x04;
    asm_emit1(a, rex);
    asm_emit1(a, 0x8B);
    asm_emit1(a, (uint8_t)(0x05 | (enc << 3)));
    asm_reloc_iat(a, sym);
}

/* call [abs32]  -- 32-bit import call via IAT */
void asm_call_import32(Assembler *a, const char *sym) {
    asm_emit2(a,0xFF,0x15);
    asm_reloc_iat(a, sym);
}

/* near call to a label (within .text) */
void asm_call_direct(Assembler *a, int label_id) {
    asm_emit1(a,0xE8); asm_emit_fixup(a,label_id);
}

/* call rel32 to a statically-linked symbol (from a .a archive).
 * Emits E8 + placeholder; ELF builder patches the displacement. */
void asm_call_static(Assembler *a, const char *sym) {
    asm_emit1(a, 0xE8);
    add_reloc(a, RELOC_STATIC_REL32, sym, 0);
}

void asm_int3(Assembler *a) { asm_emit1(a,0xCC); }

/* =========================================================================
 * Inline-asm (GCC extended asm) support primitives
 * ========================================================================= */
void asm_lock_prefix(Assembler *a) { asm_emit1(a,0xF0); }

/* All of the following take an explicit byte WIDTH (1/2/4/8) rather than
 * silently following the assembler's own target mode (a->is_64bit) — an
 * inline-asm operand's real width comes from its GAS mnemonic suffix
 * (b/w/l/q) or, absent one, the C variable's own declared type, and an
 * "int" (4 bytes) stays 4 bytes even when compiling for a 64-bit target.
 * Getting this wrong doesn't crash outright: XCHG/CMPXCHG on a too-wide
 * operand silently reads/writes the 4 bytes *past* a real 4-byte variable
 * as well, corrupting whatever stack slot happens to sit next to it. */
static void w_prefix(Assembler *a, int width, Reg reg, Reg rm) {
    if (width==2) asm_emit1(a,0x66);
    if (width==8) rex_w(a,reg,rm);
    else if (needs_rex_r(reg)||needs_rex_r(rm)) {
        uint8_t rex=0x40; if(needs_rex_r(reg)) rex|=0x44; if(needs_rex_r(rm)) rex|=0x41; asm_emit1(a,rex);
    }
}
static void w_prefix_single(Assembler *a, int width, Reg r) {
    if (width==2) asm_emit1(a,0x66);
    if (width==8) rex_w_single(a,r);
    else if (needs_rex_r(r)) asm_emit1(a,0x41);
}

void asm_xchg_reg_reg_w(Assembler *a, Reg r1, Reg r2, int width) {
    w_prefix(a,width,r1,r2);
    asm_emit1(a, width==1 ? 0x86 : 0x87);
    modrm_rr(a,r1,r2);
}
void asm_xchg_reg_mem_w(Assembler *a, Reg reg, Reg base, int disp, int width) {
    w_prefix(a,width,reg,base);
    asm_emit1(a, width==1 ? 0x86 : 0x87);
    modrm_disp(a,reg,base,disp);
}
void asm_cmpxchg_mem_reg_w(Assembler *a, Reg base, int disp, Reg src, int width) {
    /* CMPXCHG [base+disp], src — 0F B1 /r (0F B0 for the 8-bit form).
     * Compares implicit AL/AX/EAX/RAX against [base+disp]; if equal, stores
     * src there (else loads [base+disp] back into AL/AX/EAX/RAX). Caller
     * is responsible for having the comparand already in the accumulator. */
    w_prefix(a,width,src,base);
    asm_emit2(a,0x0F, width==1 ? 0xB0 : 0xB1);
    modrm_disp(a,src,base,disp);
}
/* Backward-compatible wrappers at native/pointer width (a->is_64bit). */
void asm_xchg_reg_reg(Assembler *a, Reg r1, Reg r2) { asm_xchg_reg_reg_w(a,r1,r2, a->is_64bit?8:4); }
void asm_xchg_reg_mem(Assembler *a, Reg reg, Reg base, int disp) { asm_xchg_reg_mem_w(a,reg,base,disp, a->is_64bit?8:4); }
void asm_cmpxchg_mem_reg(Assembler *a, Reg base, int disp, Reg src) { asm_cmpxchg_mem_reg_w(a,base,disp,src, a->is_64bit?8:4); }

void asm_pushf(Assembler *a) { asm_emit1(a,0x9C); }
void asm_popf (Assembler *a) { asm_emit1(a,0x9D); }
void asm_nop  (Assembler *a) { asm_emit1(a,0x90); }
void asm_pause(Assembler *a) { asm_emit2(a,0xF3,0x90); }
void asm_hlt  (Assembler *a) { asm_emit1(a,0xF4); }
void asm_cpuid(Assembler *a) { asm_emit2(a,0x0F,0xA2); }
void asm_rdtsc(Assembler *a) { asm_emit2(a,0x0F,0x31); }
void asm_int_imm8(Assembler *a, uint8_t n) { asm_emit1(a,0xCD); asm_emit1(a,n); }

void asm_inc_reg_w(Assembler *a, Reg r, int width) {
    w_prefix_single(a,width,r);
    asm_emit1(a, width==1 ? 0xFE : 0xFF); asm_emit1(a,0xC0|reg_enc(r));
}
void asm_dec_reg_w(Assembler *a, Reg r, int width) {
    w_prefix_single(a,width,r);
    asm_emit1(a, width==1 ? 0xFE : 0xFF); asm_emit1(a,0xC8|reg_enc(r));
}
void asm_inc_reg(Assembler *a, Reg r) { asm_inc_reg_w(a,r, a->is_64bit?8:4); }
void asm_dec_reg(Assembler *a, Reg r) { asm_dec_reg_w(a,r, a->is_64bit?8:4); }

void asm_alu_reg_imm_w(Assembler *a, int op, Reg r, long long imm, int width) {
    w_prefix_single(a,width,r);
    if (width==1) {
        asm_emit1(a,0x80); asm_emit1(a,(uint8_t)(0xC0|(op<<3)|reg_enc(r))); asm_emit1(a,(uint8_t)(int8_t)imm);
    } else if (imm>=-128 && imm<=127) {
        asm_emit1(a,0x83); asm_emit1(a,(uint8_t)(0xC0|(op<<3)|reg_enc(r))); asm_emit1(a,(uint8_t)(int8_t)imm);
    } else if (width==2) {
        asm_emit1(a,0x81); asm_emit1(a,(uint8_t)(0xC0|(op<<3)|reg_enc(r))); asm_emit_u16(a,(uint16_t)(int16_t)imm);
    } else {
        asm_emit1(a,0x81); asm_emit1(a,(uint8_t)(0xC0|(op<<3)|reg_enc(r))); asm_emit_u32(a,(uint32_t)(int32_t)imm);
    }
}
void asm_alu_reg_imm(Assembler *a, int op, Reg r, long long imm) { asm_alu_reg_imm_w(a,op,r,imm, a->is_64bit?8:4); }

/* =========================================================================
 * x64 Windows ABI argument placement
 * Result is in RAX; move it to the right arg register.
 * Indices 0-3: RCX, RDX, R8, R9
 * Index >= 4: already on stack via sub RSP (caller-managed)
 * ========================================================================= */
/* Avoid static Reg array (squash can't init non-zero globals). */
static Reg win64_arg_reg(int i) {
    if (i==0) return REG_RCX;
    if (i==1) return REG_RDX;
    if (i==2) return REG_R8;
    return REG_R9;
}

static Reg sysv_arg_reg(int i) {
    if (i==0) return REG_RDI;
    if (i==1) return REG_RSI;
    if (i==2) return REG_RDX;
    if (i==3) return REG_RCX;
    if (i==4) return REG_R8;
    return REG_R9;
}

void asm_arg_from_rax(Assembler *a, int arg_index) {
    if (!a->is_64bit) return; /* 32-bit uses push */
    if (a->is_linux) {
        if (arg_index < 6) {
            Reg r = sysv_arg_reg(arg_index);
            if (r != REG_RAX)
                asm_mov_reg_reg(a, r, REG_RAX);
        } else {
            /* SysV: extra args at [RSP + (arg_index-6)*8] (no shadow space) */
            asm_mov_mem_reg(a, REG_RSP, (arg_index-6)*8, REG_RAX);
        }
    } else {
        if (arg_index < 4) {
            Reg r = win64_arg_reg(arg_index);
            if (r != REG_RAX)
                asm_mov_reg_reg(a, r, REG_RAX);
        } else {
            /* store at [RSP + 32 + (arg_index-4)*8] */
            asm_mov_mem_reg(a, REG_RSP, 32 + (arg_index-4)*8, REG_RAX);
        }
    }
}

/* =========================================================================
 * asm_call_reg — call through a register (function pointer)
 * 64-bit: FF /2 ModRM=11_010_reg  (e.g. FF D0 = call rax)
 * 32-bit: FF /2 ModRM=11_010_reg  (e.g. FF D0 = call eax)
 * For extended regs (r8-r15) we need a REX.B prefix.
 * ========================================================================= */
void asm_call_reg(Assembler *a, Reg reg) {
    int enc = reg_enc(reg);
    if (a->is_64bit && needs_rex_r(reg)) {
        /* REX.B to access r8–r15 as call target */
        asm_emit1(a, 0x41);
    }
    asm_emit1(a, 0xFF);
    asm_emit1(a, (uint8_t)(0xD0 | enc));
}

/* =========================================================================
 * Static / global data section helpers
 * ========================================================================= */
void asm_init_data(Assembler *a) {
    a->data_cap = 4096;
    a->data_buf = calloc(a->data_cap, 1);
    a->data_len = 0;
    a->data_sym_cap = 64;
    a->data_syms    = malloc(a->data_sym_cap * sizeof(DataSymbol));
    a->data_sym_count = 0;
}

int asm_data_alloc(Assembler *a, const char *name, int size) {
    /* Grow buffer if needed */
    while (a->data_len + size > a->data_cap) {
        a->data_cap *= 2;
        a->data_buf = realloc(a->data_buf, a->data_cap);
    }
    int off = a->data_len;
    memset(a->data_buf + off, 0, size);
    a->data_len += size;
    /* Align to next 8 bytes */
    int pad = (8 - (a->data_len & 7)) & 7;
    while (pad-- > 0 && a->data_len < a->data_cap) a->data_buf[a->data_len++] = 0;

    /* Register symbol */
    if (name && a->data_sym_count < a->data_sym_cap) {
        DataSymbol *ds = &a->data_syms[a->data_sym_count++];
        ds->name   = my_strdup(name);
        ds->offset = off;
        ds->size   = size;
    }
    return off;
}

void asm_data_write32(Assembler *a, int offset, int32_t value) {
    if (offset + 4 <= a->data_len)
        memcpy(a->data_buf + offset, &value, 4);
}

void asm_data_write64(Assembler *a, int offset, int64_t value) {
    if (offset + 8 <= a->data_len)
        memcpy(a->data_buf + offset, &value, 8);
}

uint8_t *asm_data_bytes(Assembler *a, int *out_len) {
    *out_len = a->data_len;
    return a->data_buf;
}

/* =========================================================================
 * asm_load_func_addr — load address of a code-section label into dst
 * 64-bit: LEA dst, [RIP + disp32]  (RIP-relative, resolved as a fixup)
 * 32-bit: MOV dst, imm32           (absolute VA, RELOC_TEXT_ABS32)
 * ========================================================================= */
void asm_load_func_addr(Assembler *a, Reg dst, int label_id) {
    if (a->is_64bit) {
        /* 48 8D 05 disp32  (for RAX; adjust REX for other regs)
         * Encoding: REX.W + 8D /r, ModRM=00_reg_101 (RIP-relative) */
        int enc = reg_enc(dst);
        uint8_t rex = 0x48;
        if (needs_rex_r(dst)) rex |= 0x04;  /* REX.R */
        asm_emit1(a, rex);
        asm_emit1(a, 0x8D);
        uint8_t modrm = (uint8_t)(0x05 | (enc << 3));
        asm_emit1(a, modrm);
        /* Emit a fixup for the 4-byte disp32 field */
        asm_emit_fixup(a, label_id);
    } else {
        /* MOV dst, imm32  followed by a RELOC_TEXT_ABS32 relocation */
        int enc = reg_enc(dst);
        asm_emit1(a, (uint8_t)(0xB8 | enc));  /* MOV r32, imm32 */
        /* Record relocation: patch this 4-byte slot with abs VA of label.
         * add_reloc() already emits the 4-byte placeholder internally.  */
        char lbl_key[32]; snprintf(lbl_key, sizeof lbl_key, "__lbl_%d", label_id);
        add_reloc(a, RELOC_TEXT_ABS32, lbl_key, 0);
        /* NOTE: do NOT call asm_emit_u32 here — add_reloc already did it */
    }
}

/* =========================================================================
 * asm_resolve_text_relocs — patch RELOC_TEXT_ABS32 entries in the code buffer.
 * Called after asm_resolve() so all label positions are final.
 * text_rva: the .text section RVA; image_base: PE image base.
 * For each RELOC_TEXT_ABS32 with symbol "__lbl_NNN":
 *   parse NNN as label_id, look up its code offset, compute abs VA, patch.
 * ========================================================================= */
void asm_resolve_text_relocs(Assembler *a, uint32_t text_rva, uint64_t image_base) {
    for (int i = 0; i < a->reloc_count; i++) {
        Relocation *r = &a->relocs[i];
        if (r->kind != RELOC_TEXT_ABS32) continue;

        /* Parse label_id from symbol name "__lbl_NNN" */
        if (strncmp(r->symbol, "__lbl_", 6) != 0) continue;
        int label_id = atoi(r->symbol + 6);

        /* Find the label's code offset.
         * Labels are stored in array order; label_id is the index. */
        int label_off = -1;
        if (label_id >= 0 && label_id < a->label_count) {
            label_off = a->labels[label_id].offset;
        }
        if (label_off < 0) {
            printf("asm_resolve_text_relocs: label %d not found\n", label_id);
            continue;
        }

        /* Absolute VA = image_base + text_rva + label_off */
        uint32_t abs_va = (uint32_t)(image_base + text_rva + label_off);
        int patch = r->offset;
        a->code[patch+0] = (uint8_t)(abs_va);
        a->code[patch+1] = (uint8_t)(abs_va >> 8);
        a->code[patch+2] = (uint8_t)(abs_va >> 16);
        a->code[patch+3] = (uint8_t)(abs_va >> 24);
    }
}

/* =========================================================================
 * Writable data section relocation helpers
 * ========================================================================= */
void asm_reloc_wdata(Assembler *a, const char *sym) {
    RelocKind k = a->is_64bit ? RELOC_WDATA_REL32 : RELOC_WDATA_ABS32;
    add_reloc(a, k, sym, 0);
}

void asm_lea_rip_wdata(Assembler *a, Reg dst, const char *sym) {
    int _is64 = a->is_64bit;
    if (!_is64) {
        /* 32-bit: mov dst, abs_addr */
        asm_emit1(a, (uint8_t)(0xB8 | reg_enc(dst)));
        asm_reloc_wdata(a, sym);
        return;
    }
    /* 64-bit: 48/4C 8D reg [RIP+disp32] */
    uint8_t rex = 0x48;
    int enc = reg_enc(dst);
    if (needs_rex_r(dst)) rex |= 0x04;
    asm_emit1(a, rex);
    asm_emit1(a, 0x8D);
    asm_emit1(a, (uint8_t)(0x05 | (enc << 3)));
    asm_reloc_wdata(a, sym);
}

/* Add immediate to RAX/EAX — used for struct field offset */
void asm_add_imm(Assembler *a, Reg dst, int imm) {
    int enc = reg_enc(dst);
    if (imm == 0) return;
    if (imm >= -128 && imm <= 127) {
        /* add reg, imm8 */
        if (a->is_64bit) { asm_emit1(a,0x48); asm_emit2(a,0x83,0xC0|(uint8_t)enc); }
        else              { asm_emit2(a,0x83,0xC0|(uint8_t)enc); }
        asm_emit1(a,(uint8_t)(int8_t)imm);
    } else {
        /* add reg, imm32 */
        if (a->is_64bit) { asm_emit1(a,0x48); asm_emit2(a,0x81,0xC0|(uint8_t)enc); }
        else              { asm_emit2(a,0x81,0xC0|(uint8_t)enc); }
        asm_emit_u32(a,(uint32_t)imm);
    }
}

/* =========================================================================
 * SSE2 / x87 floating-point instruction emitters
 * ========================================================================= */

/* Helper: emit SSE2 instruction prefix + opcode + ModRM for xmm,mem */
static void sse2_xmm_mem(Assembler *a, uint8_t pfx, uint8_t op,
                          int xmm, Reg base, int disp) {
    asm_emit1(a, pfx);           /* F2 or F3 or 66 */
    if (a->is_64bit) asm_emit1(a, 0x48); /* REX.W not always needed but safe for addressing */
    asm_emit2(a, 0x0F, op);
    /* ModRM: mod=10(disp32) or mod=01(disp8), reg=xmm&7, rm=base */
    int enc = reg_enc(base);
    if (disp >= -128 && disp <= 127) {
        asm_emit1(a, (uint8_t)(0x40 | ((xmm&7)<<3) | enc));
        asm_emit1(a, (uint8_t)(int8_t)disp);
    } else {
        asm_emit1(a, (uint8_t)(0x80 | ((xmm&7)<<3) | enc));
        asm_emit_u32(a, (uint32_t)disp);
    }
}

/* movsd xmm,[base+disp]  — F2 0F 10 /r */
void asm_movsd_load(Assembler *a, int xmm_dst, Reg base, int disp) {
    asm_emit1(a,0xF2);
    if (a->is_64bit) { /* REX if needed for r8+ regs */ }
    asm_emit2(a,0x0F,0x10);
    int enc=reg_enc(base);
    if (disp>=-128&&disp<=127) {
        asm_emit1(a,(uint8_t)(0x40|((xmm_dst&7)<<3)|enc));
        if (enc==4) asm_emit1(a,0x24); /* SIB: base=rsp, no index */
        asm_emit1(a,(uint8_t)(int8_t)disp);
    } else {
        asm_emit1(a,(uint8_t)(0x80|((xmm_dst&7)<<3)|enc));
        if (enc==4) asm_emit1(a,0x24);
        asm_emit_u32(a,(uint32_t)disp);
    }
}

/* movsd [base+disp],xmm  — F2 0F 11 /r */
void asm_movsd_store(Assembler *a, Reg base, int disp, int xmm_src) {
    asm_emit1(a,0xF2);
    asm_emit2(a,0x0F,0x11);
    int enc=reg_enc(base);
    if (disp>=-128&&disp<=127) {
        asm_emit1(a,(uint8_t)(0x40|((xmm_src&7)<<3)|enc));
        if (enc==4) asm_emit1(a,0x24); /* SIB: base=rsp, no index */
        asm_emit1(a,(uint8_t)(int8_t)disp);
    } else {
        asm_emit1(a,(uint8_t)(0x80|((xmm_src&7)<<3)|enc));
        if (enc==4) asm_emit1(a,0x24);
        asm_emit_u32(a,(uint32_t)disp);
    }
}

/* movsd xmm_dst,[rip+sym]  — for loading float constants */
void asm_movsd_rip(Assembler *a, int xmm_dst, const char *sym) {
    asm_emit1(a,0xF2); asm_emit2(a,0x0F,0x10);
    asm_emit1(a,(uint8_t)(0x05|((xmm_dst&7)<<3))); /* ModRM: rip+disp32 */
    add_reloc(a, RELOC_DATA_REL32, sym, 0);
}

/* movsd xmm_dst,xmm_src */
void asm_movsd_xmm(Assembler *a, int dst, int src) {
    asm_emit1(a,0xF2); asm_emit2(a,0x0F,0x10);
    asm_emit1(a,(uint8_t)(0xC0|((dst&7)<<3)|(src&7)));
}

static void sse2_xmm_xmm(Assembler *a, uint8_t pfx, uint8_t op, int dst, int src) {
    asm_emit1(a,pfx); asm_emit2(a,0x0F,op);
    asm_emit1(a,(uint8_t)(0xC0|((dst&7)<<3)|(src&7)));
}

void asm_addsd (Assembler *a,int d,int s){sse2_xmm_xmm(a,0xF2,0x58,d,s);}
void asm_subsd (Assembler *a,int d,int s){sse2_xmm_xmm(a,0xF2,0x5C,d,s);}
void asm_mulsd (Assembler *a,int d,int s){sse2_xmm_xmm(a,0xF2,0x59,d,s);}
void asm_divsd (Assembler *a,int d,int s){sse2_xmm_xmm(a,0xF2,0x5E,d,s);}
void asm_xorpd (Assembler *a,int d,int s){sse2_xmm_xmm(a,0x66,0x57,d,s);}
void asm_xorps (Assembler *a,int d,int s){asm_emit1(a,0x0F);asm_emit1(a,0x57);asm_emit1(a,(uint8_t)(0xC0|((d&7)<<3)|(s&7)));}

/* ucomisd xmm0,xmm1  — 66 0F 2E /r */
void asm_ucomisd(Assembler *a, int x0, int x1) {
    asm_emit1(a,0x66); asm_emit2(a,0x0F,0x2E);
    asm_emit1(a,(uint8_t)(0xC0|((x0&7)<<3)|(x1&7)));
}

/* cvtsi2sd xmm,r/m32  — F2 0F 2A /r */
void asm_cvtsi2sd(Assembler *a, int xmm_dst, Reg int_src) {
    asm_emit1(a,0xF2);
    if (a->is_64bit) asm_emit1(a,0x48); /* REX.W for 64-bit int source */
    asm_emit2(a,0x0F,0x2A);
    asm_emit1(a,(uint8_t)(0xC0|((xmm_dst&7)<<3)|reg_enc(int_src)));
}

/* cvttsd2si r32,xmm  — F2 0F 2C /r (truncate toward zero) */
void asm_cvttsd2si(Assembler *a, Reg int_dst, int xmm_src) {
    asm_emit1(a,0xF2);
    asm_emit2(a,0x0F,0x2C);
    asm_emit1(a,(uint8_t)(0xC0|(reg_enc(int_dst)<<3)|(xmm_src&7)));
}

/* movss xmm,[base+disp]  — F3 0F 10 */
void asm_movss_load(Assembler *a, int xmm_dst, Reg base, int disp) {
    asm_emit1(a,0xF3); asm_emit2(a,0x0F,0x10);
    int enc=reg_enc(base);
    if (disp>=-128&&disp<=127){asm_emit1(a,(uint8_t)(0x40|((xmm_dst&7)<<3)|enc));if(enc==4)asm_emit1(a,0x24);asm_emit1(a,(uint8_t)(int8_t)disp);}
    else{asm_emit1(a,(uint8_t)(0x80|((xmm_dst&7)<<3)|enc));if(enc==4)asm_emit1(a,0x24);asm_emit_u32(a,(uint32_t)disp);}
}

/* movss [base+disp],xmm */
void asm_movss_store(Assembler *a, Reg base, int disp, int xmm_src) {
    asm_emit1(a,0xF3); asm_emit2(a,0x0F,0x11);
    int enc=reg_enc(base);
    if (disp>=-128&&disp<=127){asm_emit1(a,(uint8_t)(0x40|((xmm_src&7)<<3)|enc));if(enc==4)asm_emit1(a,0x24);asm_emit1(a,(uint8_t)(int8_t)disp);}
    else{asm_emit1(a,(uint8_t)(0x80|((xmm_src&7)<<3)|enc));if(enc==4)asm_emit1(a,0x24);asm_emit_u32(a,(uint32_t)disp);}
}

void asm_cvtss2sd(Assembler *a,int d,int s){sse2_xmm_xmm(a,0xF3,0x5A,d,s);}
void asm_cvtsd2ss(Assembler *a,int d,int s){sse2_xmm_xmm(a,0xF2,0x5A,d,s);}

/* Push/pop XMM via stack */
void asm_push_xmm(Assembler *a, int xmm) {
    asm_sub_rsp(a,16); /* 16-byte aligned slot */
    asm_emit1(a,0xF2); asm_emit2(a,0x0F,0x11);
    asm_emit1(a,(uint8_t)(0x04|((xmm&7)<<3))); /* ModRM: [rsp] with SIB */
    asm_emit1(a,0x24); /* SIB: [rsp] */
}
void asm_pop_xmm(Assembler *a, int xmm) {
    asm_emit1(a,0xF2); asm_emit2(a,0x0F,0x10);
    asm_emit1(a,(uint8_t)(0x04|((xmm&7)<<3))); asm_emit1(a,0x24);
    asm_add_rsp(a,16);
}

/* x87 helpers (32-bit mode) */
void asm_fld_mem64 (Assembler *a,Reg b,int d){
    int enc=reg_enc(b);
    if(d>=-128&&d<=127){asm_emit1(a,0xDD);asm_emit1(a,(uint8_t)(0x40|enc));if(enc==4)asm_emit1(a,0x24);asm_emit1(a,(uint8_t)(int8_t)d);}
    else{asm_emit1(a,0xDD);asm_emit1(a,(uint8_t)(0x80|enc));if(enc==4)asm_emit1(a,0x24);asm_emit_u32(a,(uint32_t)d);}
}
void asm_fstp_mem64(Assembler *a,Reg b,int d){
    int enc=reg_enc(b);
    if(d>=-128&&d<=127){asm_emit1(a,0xDD);asm_emit1(a,(uint8_t)(0x58|enc));if(enc==4)asm_emit1(a,0x24);asm_emit1(a,(uint8_t)(int8_t)d);}
    else{asm_emit1(a,0xDD);asm_emit1(a,(uint8_t)(0x98|enc));if(enc==4)asm_emit1(a,0x24);asm_emit_u32(a,(uint32_t)d);}
}
void asm_fild_mem32 (Assembler *a,Reg b,int d){
    int enc=reg_enc(b);
    if(d>=-128&&d<=127){
        asm_emit1(a,0xDB);
        asm_emit1(a,(uint8_t)(0x40|enc));
        if(enc==4) asm_emit1(a,0x24); /* SIB: base=esp, no index */
        asm_emit1(a,(uint8_t)(int8_t)d);
    } else {
        asm_emit1(a,0xDB);
        asm_emit1(a,(uint8_t)(0x80|enc));
        if(enc==4) asm_emit1(a,0x24); /* SIB: base=esp, no index */
        asm_emit_u32(a,(uint32_t)d);
    }
}
void asm_fistp_mem32(Assembler *a,Reg b,int d){
    asm_emit1(a,0xD9); asm_emit1(a,0xFC); /* frndint first */
    if(d>=-128&&d<=127){asm_emit1(a,0xDB);asm_emit1(a,(uint8_t)(0x58|reg_enc(b)));asm_emit1(a,(uint8_t)(int8_t)d);}
    else{asm_emit1(a,0xDB);asm_emit1(a,(uint8_t)(0x98|reg_enc(b)));asm_emit_u32(a,(uint32_t)d);}
}
void asm_faddp (Assembler *a){asm_emit2(a,0xDE,0xC1);}
void asm_fsubp (Assembler *a){asm_emit2(a,0xDE,0xE9);}
void asm_fsubrp(Assembler *a){asm_emit2(a,0xDE,0xE1);}
void asm_fmulp (Assembler *a){asm_emit2(a,0xDE,0xC9);}
void asm_fdivp (Assembler *a){asm_emit2(a,0xDE,0xF9);}
void asm_fdivrp(Assembler *a){asm_emit2(a,0xDE,0xF1);}
void asm_fcompp(Assembler *a){asm_emit2(a,0xDE,0xD9);}
void asm_fnstsw(Assembler *a){asm_emit2(a,0xDF,0xE0);} /* fnstsw ax */
void asm_sahf  (Assembler *a){asm_emit1(a,0x9E);}
void asm_fld1  (Assembler *a){asm_emit2(a,0xD9,0xE8);}
void asm_fldz  (Assembler *a){asm_emit2(a,0xD9,0xEE);}
void asm_fldpi (Assembler *a){asm_emit2(a,0xD9,0xEB);}
void asm_fchs  (Assembler *a){asm_emit2(a,0xD9,0xE0);}
void asm_fabs_x87(Assembler *a){asm_emit2(a,0xD9,0xE1);}
void asm_fsqrt (Assembler *a){asm_emit2(a,0xD9,0xFA);}
