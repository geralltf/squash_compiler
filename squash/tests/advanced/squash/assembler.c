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
}
void asm_free(Assembler *a) {
    free(a->code);
    for (int i=0;i<a->reloc_count;i++) free(a->relocs[i].symbol);
    free(a->relocs);
    for (int i=0;i<a->label_count;i++) free(a->labels[i].name);
    free(a->labels);
    for (int i=0;i<a->fixup_count;i++) ; /* no heap in fixup */
    free(a->fixups);
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
            fprintf(stderr,"asm_resolve: undefined label %d\n",lid); exit(1);
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
void asm_leave(Assembler *a) { asm_emit1(a, 0xC9); }
void asm_ret  (Assembler *a) { asm_emit1(a, 0xC3); }

/* =========================================================================
 * Data movement
 * ========================================================================= */
void asm_mov_reg_imm(Assembler *a, Reg dst, long long imm) {
    if (a->is_64bit) {
        if (imm >= -2147483648LL && imm <= 2147483647LL) {
            /* mov eax, imm32  (zero-extends to rax) */
            if (needs_rex_r(dst)) asm_emit1(a,0x41);
            asm_emit1(a, 0xB8 | reg_enc(dst));
            asm_emit_u32(a,(uint32_t)(int32_t)imm);
        } else {
            /* movabs rax, imm64 */
            rex_w_single(a, dst);
            asm_emit1(a, 0xB8 | reg_enc(dst));
            asm_emit_u64(a,(uint64_t)imm);
        }
    } else {
        asm_emit1(a, 0xB8 | reg_enc(dst));
        asm_emit_u32(a,(uint32_t)(int32_t)imm);
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
    if (!a->is_64bit) { fprintf(stderr,"asm_lea_rip_data: 32-bit not supported\n"); exit(1); }
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
    if (a->is_64bit) { rex_w_single(a,r); asm_emit1(a,0xF7); asm_emit1(a,0xD8|reg_enc(r)); }
    else { asm_emit1(a,0xF7); asm_emit1(a,0xD8|reg_enc(r)); }
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
    asm_emit3(a,0x0F, 0x90|(uint8_t)cc, 0xC0);
}
void asm_movzx_rax_al(Assembler *a) {
    if (a->is_64bit) asm_emit4(a,0x48,0x0F,0xB6,0xC0);
    else asm_emit3(a,0x0F,0xB6,0xC0);
}

/* =========================================================================
 * Control flow
 * ========================================================================= */
void asm_jmp_label(Assembler *a, int label_id) {
    asm_emit1(a,0xE9); asm_emit_fixup(a,label_id);
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
