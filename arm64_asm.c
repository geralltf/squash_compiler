#include "arm64_asm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* forward declaration from assembler.c */
char *my_strdup(const char *s);

/* =========================================================================
 * Init / free
 * ========================================================================= */
void a64_asm_init(Arm64Asm *a, int is_linux) {
    memset(a, 0, sizeof *a);
    a->is_linux   = is_linux;
    a->code_cap   = 65536;
    a->code       = (uint8_t*)malloc(a->code_cap);
    a->reloc_cap  = 512;
    a->relocs     = (Relocation*)malloc(a->reloc_cap * sizeof(Relocation));
    a->label_cap  = 128;
    a->labels     = (Label*)malloc(a->label_cap * sizeof(Label));
    a->fixup_cap  = 512;
    a->fixups     = (Fixup*)malloc(a->fixup_cap * sizeof(Fixup));
    a->rdata_cap  = 65536;
    a->rdata      = (uint8_t*)malloc(a->rdata_cap);
    a->wdata_cap  = 4096;
    a->wdata      = (uint8_t*)calloc(a->wdata_cap, 1);
}

void a64_asm_free(Arm64Asm *a) {
    int i;
    free(a->code);
    for (i = 0; i < a->reloc_count; i++) free(a->relocs[i].symbol);
    free(a->relocs);
    for (i = 0; i < a->label_count; i++) free(a->labels[i].name);
    free(a->labels);
    free(a->fixups);
    free(a->rdata);
    free(a->wdata);
}

/* =========================================================================
 * Instruction emission
 * ========================================================================= */
void a64_emit(Arm64Asm *a, uint32_t instr) {
    if (a->code_len + 4 > a->code_cap) {
        a->code_cap *= 2;
        a->code = (uint8_t*)realloc(a->code, a->code_cap);
    }
    /* little-endian 32-bit write */
    a->code[a->code_len+0] = (uint8_t)(instr);
    a->code[a->code_len+1] = (uint8_t)(instr >> 8);
    a->code[a->code_len+2] = (uint8_t)(instr >> 16);
    a->code[a->code_len+3] = (uint8_t)(instr >> 24);
    a->code_len += 4;
}

void a64_patch(Arm64Asm *a, int off, uint32_t instr) {
    a->code[off+0] = (uint8_t)(instr);
    a->code[off+1] = (uint8_t)(instr >> 8);
    a->code[off+2] = (uint8_t)(instr >> 16);
    a->code[off+3] = (uint8_t)(instr >> 24);
}

/* =========================================================================
 * Labels
 * ========================================================================= */
int a64_new_label(Arm64Asm *a, const char *name) {
    if (a->label_count == a->label_cap) {
        a->label_cap *= 2;
        a->labels = (Label*)realloc(a->labels, a->label_cap * sizeof(Label));
    }
    int id = a->label_count++;
    a->labels[id].name   = my_strdup(name ? name : "");
    a->labels[id].offset = -1;
    return id;
}

void a64_def_label(Arm64Asm *a, int id) {
    a->labels[id].offset = a->code_len;
}

int a64_find_label(Arm64Asm *a, const char *name) {
    int i;
    for (i = 0; i < a->label_count; i++)
        if (strcmp(a->labels[i].name, name) == 0)
            return i;
    return -1;
}

/* =========================================================================
 * Fixups (forward branch references)
 * ========================================================================= */
void a64_add_fixup(Arm64Asm *a, int patch_off, int label_id) {
    if (a->fixup_count == a->fixup_cap) {
        a->fixup_cap *= 2;
        a->fixups = (Fixup*)realloc(a->fixups, a->fixup_cap * sizeof(Fixup));
    }
    a->fixups[a->fixup_count].patch_offset = patch_off;
    a->fixups[a->fixup_count].label_id     = label_id;
    a->fixup_count++;
}

/* Resolve all outstanding fixups — called after function body is done */
void a64_resolve(Arm64Asm *a) {
    int i;
    for (i = 0; i < a->fixup_count; i++) {
        int patch = a->fixups[i].patch_offset;
        int lid   = a->fixups[i].label_id;
        if (lid < 0 || lid >= a->label_count) continue;
        int target = a->labels[lid].offset;
        if (target < 0) {
            printf("arm64: unresolved label id %d (name='%s')\n", lid, a->labels[lid].name ? a->labels[lid].name : "?");
            continue;
        }
        /* Read current instruction at patch location */
        uint32_t instr;
        memcpy(&instr, a->code + patch, 4);

        /* Determine instruction class by bits [31:24] or full opcode */
        uint32_t top8 = (instr >> 24) & 0xFF;

        if ((top8 & 0xFE) == 0x54) {
            /* B.cond: imm19 at bits [23:5] */
            int delta = (target - patch) / 4;
            instr = (instr & 0xFF00001Fu) | ((uint32_t)(delta & 0x7FFFF) << 5);
        } else if ((top8 & 0x7C) == 0x14) {
            /* B (0x14-0x17) / BL (0x94-0x97): imm26 at bits [25:0].
             * Mask excludes bit7 (the link bit) so both share this case. */
            int delta = (target - patch) / 4;
            instr = (instr & 0xFC000000u) | ((uint32_t)(delta & 0x3FFFFFF));
        } else if ((top8 & 0xFE) == 0xB4 || (top8 & 0xFE) == 0x34) {
            /* CBZ/CBNZ, 64-bit (0xB4/0xB5) or 32-bit W-register form
             * (0x34/0x35): imm19 at bits [23:5] either way. */
            int delta = (target - patch) / 4;
            instr = (instr & 0xFF00001Fu) | ((uint32_t)(delta & 0x7FFFF) << 5);
        } else if ((top8 & 0x9F) == 0x10) {
            /* ADR Xd, #imm21 — byte-level PC-relative (no /4, no page
             * truncation), used to load a function's address as a value.
             * immlo(2 bits) is bits[30:29], immhi(19 bits) is bits[23:5]. */
            int delta = target - patch;
            uint32_t rd = instr & 0x1Fu;
            uint32_t lo2 = (uint32_t)delta & 3u;
            uint32_t hi19 = ((uint32_t)delta >> 2) & 0x7FFFFu;
            instr = 0x10000000u | (lo2 << 29) | (hi19 << 5) | rd;
        } else {
            printf("arm64: unknown fixup instruction 0x%08X at %d\n", instr, patch);
            continue;
        }
        a64_patch(a, patch, instr);
    }
    /* Don't reset fixup_count: a64_resolve() is called after every nested
     * if/loop/switch construct finishes, not just once per function. An
     * outer loop's exit-branch fixup is often still unresolved (target<0)
     * when an inner construct's resolve() call runs, so entries must stay
     * in the array to be retried by a later call once their label is
     * finally defined. Already-resolved entries are simply re-patched to
     * the same value, which is harmless. */
}

/* =========================================================================
 * Relocations
 * ========================================================================= */
void a64_add_reloc(Arm64Asm *a, int off, RelocKind kind, const char *sym, int addend) {
    if (a->reloc_count == a->reloc_cap) {
        a->reloc_cap *= 2;
        a->relocs = (Relocation*)realloc(a->relocs, a->reloc_cap * sizeof(Relocation));
    }
    Relocation *r = &a->relocs[a->reloc_count++];
    r->offset  = off;
    r->kind    = kind;
    r->symbol  = my_strdup(sym);
    r->addend  = addend;
}

void a64_call_static(Arm64Asm *a, const char *sym) {
    int patch = a->code_len;
    a64_emit(a, a64_BL(0));
    a64_add_reloc(a, patch, RELOC_A64_STATIC_BL, sym, 0);
}
