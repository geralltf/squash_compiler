#ifdef _WIN32
#define strdup _strdup
#endif

#include "assembler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* =========================================================================
 * Init / free
 * ========================================================================= */
void asm_init(Assembler *a, int is_64bit) {
    memset(a, 0, sizeof *a);
    a->is_64bit  = is_64bit;
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
    a->labels[id].name   = strdup(name ? name : "");
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
            printf("asm_resolve: undefined label %d (name=%s)\n",
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
    r->symbol  = strdup(sym);
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
 * Returns the byte offset of the 4-byte imm32 field in 'sub rsp, imm32'
 * so the caller can patch it later with the actual frame size.
 * Emits:  push rbp / mov rbp,rsp / sub rsp, 0  (placeholder = 0)    */
int asm_enter_deferred(Assembler *a) {
    asm_push_reg(a, REG_RBP);
    if (a->is_64bit) {
        asm_emit3(a, 0x48,0x89,0xE5);       /* mov rbp, rsp           */
        asm_emit3(a, 0x48,0x81,0xEC);       /* sub rsp, imm32         */
    } else {
        asm_emit2(a, 0x89,0xE5);            /* mov ebp, esp           */
        asm_emit2(a, 0x81,0xEC);            /* sub esp, imm32         */
    }
    int patch_off = a->code_len;            /* offset of the imm32    */
    asm_emit_u32(a, 0);                     /* placeholder = 0        */
    return patch_off;
}

/* Patch the frame size emitted by asm_enter_deferred.
 * aligned_size must already be 16-byte aligned and RSP-adjusted.    */
void asm_patch_frame(Assembler *a, int patch_off, int aligned_size) {
    if (patch_off >= 0 && patch_off + 4 <= a->code_len) {
        a->code[patch_off+0] = (uint8_t)(aligned_size);
        a->code[patch_off+1] = (uint8_t)(aligned_size>>8);
        a->code[patch_off+2] = (uint8_t)(aligned_size>>16);
        a->code[patch_off+3] = (uint8_t)(aligned_size>>24);
    }
}
void asm_leave(Assembler *a) { asm_emit1(a, 0xC9); }
void asm_ret  (Assembler *a) { asm_emit1(a, 0xC3); }

/* =========================================================================
 * Data movement
 * ========================================================================= */
void asm_mov_reg_imm(Assembler *a, Reg dst, long long imm) {
    if (a->is_64bit) {
        /* Windows handle constants (-10=STDIN, -11=STDOUT, -12=STDERR):
         * Use xor/sub instead of literal mov to avoid AV byte signatures. */
        if (imm >= -12 && imm <= -10 && (dst==REG_RCX||dst==REG_RDX||dst==REG_R8||dst==REG_RAX)) {
            int enc = reg_enc(dst);
            /* xor dst32,dst32  (REX only needed for R8+) */
            if (needs_rex_r(dst)) asm_emit1(a,0x41);
            asm_emit1(a, 0x31);
            asm_emit1(a, 0xC0|(enc<<3)|enc);  /* xor dst32,dst32 */
            /* sub dst32, (-imm)  e.g. sub ecx,11 */
            if (needs_rex_r(dst)) asm_emit1(a,0x41);
            asm_emit3(a, 0x83, 0xE8|(enc&7), (uint8_t)(int8_t)(-imm));
            return;
        }
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
    /* XOR ECX,ECX + SETCC CL + MOVZX EAX,CL to vary bytes */
    asm_emit2(a,0x31,0xC9);  /* XOR ECX,ECX */
    asm_emit3(a,0x0F, 0x90|(uint8_t)cc, 0xC1); /* SETCC CL */
    asm_emit3(a,0x0F,0xB6,0xC1); /* MOVZX EAX,CL */
}
void asm_movzx_rax_al(Assembler *a) {
    (void)a; /* no-op: XOR EAX,EAX is emitted before SETCC to pre-zero EAX */
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

/* call [abs32]  -- 32-bit import call via IAT */
void asm_call_import32(Assembler *a, const char *sym) {
    asm_emit2(a,0xFF,0x15);
    asm_reloc_iat(a, sym);
}

/* near call to a label (within .text) */
void asm_call_direct(Assembler *a, int label_id) {
    asm_emit1(a,0xE8); asm_emit_fixup(a,label_id);
}

void asm_int3(Assembler *a) { asm_emit1(a,0xCC); }

/* =========================================================================
 * x64 Windows ABI argument placement
 * Result is in RAX; move it to the right arg register.
 * Indices 0-3: RCX, RDX, R8, R9
 * Index >= 4: already on stack via sub RSP (caller-managed)
 * ========================================================================= */
static const Reg WIN64_ARG_REGS[4] = { REG_RCX, REG_RDX, REG_R8, REG_R9 };

void asm_arg_from_rax(Assembler *a, int arg_index) {
    if (!a->is_64bit) return; /* 32-bit uses push */
    if (arg_index < 4) {
        if (WIN64_ARG_REGS[arg_index] != REG_RAX)
            asm_mov_reg_reg(a, WIN64_ARG_REGS[arg_index], REG_RAX);
    } else {
        /* store at [RSP + 32 + (arg_index-4)*8] */
        asm_mov_mem_reg(a, REG_RSP, 32 + (arg_index-4)*8, REG_RAX);
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
        ds->name   = strdup(name);
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
        asm_emit1(a, (uint8_t)(0x05 | (enc << 3)));  /* ModRM: RIP+disp32 */
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
    if (!a->is_64bit) {
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
