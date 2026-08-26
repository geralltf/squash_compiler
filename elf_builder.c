#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include "elf_builder.h"
#include "diag.h"
#include "ast.h"

/* =========================================================================
 * Little-endian byte writers (prefixed e_ to avoid conflict with pe_builder)
 * ========================================================================= */
static void e_pu8 (uint8_t *b, int *off, uint8_t  v) { b[(*off)++] = v; }
static void e_pu16(uint8_t *b, int *off, uint16_t v) {
    b[(*off)++]=(uint8_t)(v); b[(*off)++]=(uint8_t)(v>>8);
}
static void e_pu32(uint8_t *b, int *off, uint32_t v) {
    b[(*off)++]=(uint8_t)(v); b[(*off)++]=(uint8_t)(v>>8);
    b[(*off)++]=(uint8_t)(v>>16); b[(*off)++]=(uint8_t)(v>>24);
}
static void e_pu64(uint8_t *b, int *off, uint64_t v) {
    /* Note: squash 32-bit passes uint64_t as 4-byte arg (only low 32 bits).
     * All squash ELF addresses/sizes fit in 32 bits, so hi=0 is always correct. */
    e_pu32(b,off,(uint32_t)(v));
    e_pu32(b,off,0);
}
/* Write 64-bit value as explicit hi:lo to avoid 64-bit shift issues in squash 32-bit mode.
 * Use this when the high 32 bits are non-zero (e.g. RELA r_info = sym_idx:reloc_type). */
static void e_pu64_hl(uint8_t *b, int *off, uint32_t hi, uint32_t lo) {
    e_pu32(b,off,lo); e_pu32(b,off,hi);
}
static void e_patch32(uint8_t *b, int off, uint32_t v) {
    b[off+0]=(uint8_t)(v); b[off+1]=(uint8_t)(v>>8);
    b[off+2]=(uint8_t)(v>>16); b[off+3]=(uint8_t)(v>>24);
}
static uint32_t e_read32(const uint8_t *b, int off) {
    return (uint32_t)b[off] | ((uint32_t)b[off+1]<<8) |
           ((uint32_t)b[off+2]<<16) | ((uint32_t)b[off+3]<<24);
}

/* ---- AArch64 ADRP / lo12 relocation patchers ----
 * Rewrite an existing ADRP/LDR/ADD placeholder in-place, preserving its
 * Rd/Rn register fields (already correctly emitted by codegen_arm64.c). */
static void e_patch_a64_adrp(uint8_t *text, int off, uint64_t target_vma, uint64_t pc_vma) {
    uint32_t word = e_read32(text, off);
    uint32_t rd = word & 0x1Fu;
    int64_t page_delta = (int64_t)(target_vma & ~0xFFFULL) - (int64_t)(pc_vma & ~0xFFFULL);
    int32_t imm21 = (int32_t)(page_delta >> 12);
    uint32_t lo = (uint32_t)imm21 & 3u;
    uint32_t hi = ((uint32_t)imm21 >> 2) & 0x7FFFFu;
    e_patch32(text, off, 0x90000000u | (lo<<29) | (hi<<5) | rd);
}
/* LO12 for LDR Xt,[Xn,#lo12] — offset field is scaled by 8 */
static void e_patch_a64_ldr_lo12(uint8_t *text, int off, uint64_t target_vma) {
    uint32_t word = e_read32(text, off);
    uint32_t rt = word & 0x1Fu;
    uint32_t rn = (word>>5) & 0x1Fu;
    uint32_t lo12 = (uint32_t)(target_vma & 0xFFFu);
    e_patch32(text, off, 0xF9400000u | (((lo12/8)&0xFFFu)<<10) | (rn<<5) | rt);
}
/* LO12 for ADD Xd,Xn,#lo12 — offset field is unscaled byte immediate */
static void e_patch_a64_add_lo12(uint8_t *text, int off, uint64_t target_vma) {
    uint32_t word = e_read32(text, off);
    uint32_t rd = word & 0x1Fu;
    uint32_t rn = (word>>5) & 0x1Fu;
    uint32_t lo12 = (uint32_t)(target_vma & 0xFFFu);
    e_patch32(text, off, 0x91000000u | ((lo12&0xFFFu)<<10) | (rn<<5) | rd);
}
/* 26-bit word-offset signed immediate for BL/B (direct call/branch) —
 * preserves the instruction's top 6 opcode bits (0x94/0x14) untouched. */
static void e_patch_a64_call26(uint8_t *text, int off, uint64_t target_vma, uint64_t pc_vma) {
    uint32_t word = e_read32(text, off);
    int64_t word_disp = ((int64_t)target_vma - (int64_t)pc_vma) / 4;
    uint32_t imm26 = (uint32_t)word_disp & 0x03FFFFFFu;
    e_patch32(text, off, (word & 0xFC000000u) | imm26);
}
/* Raw 8-byte little-endian absolute value (R_AARCH64_ABS64 — not an
 * instruction-bitfield patch, just a literal 64-bit data word). */
static void e_patch64(uint8_t *b, int off, uint64_t v) {
    e_patch32(b, off,   (uint32_t)v);
    e_patch32(b, off+4, (uint32_t)(v>>32));
}

/* =========================================================================
 * Import grouping by library (shared object name)
 * Uses separate flat simple static arrays — squash can't size struct arrays
 * that contain array members (it computes sizeof wrong, corrupting adjacent vars).
 * MAX_LIBS=8, MAX_FUNCS=256, FUNC_NLEN=80, LIB_NLEN=64
 *
 * MAX_FUNCS was 64 until a real program (particles_physics.c's Vulkan
 * compute rewrite) tripped it: elf_grp_add silently drops any import past
 * the cap (see the "if (elf_total_funcs >= MAX_FUNCS) return;" below), so
 * the (MAX_FUNCS+1)-th distinct imported function across the whole program
 * never gets registered/a GOT slot at all -- and every call site generated
 * for it downstream (via elf_grp_got_idx returning -1, or looking up a
 * slot that was never populated) reads as a bogus target, corrupting
 * execution in a way that surfaces at some arbitrary *other*, unrelated
 * call site rather than at the actual missing import -- a genuinely
 * confusing failure mode to debug from the symptom alone. Raised to 256,
 * comfortably above what any current demo needs (particles_physics.c's
 * Vulkan-compute rewrite alone pushed past 64 once it started calling
 * vkCreateComputePipelines/vkCmdDispatch/vkCmdBindDescriptorSets/etc. in
 * addition to its existing graphics + X11 + libc imports).
 *
 * Raised again, 256 -> 2048: self-hosting squash (compiling its own
 * ~17-file, ~30K-line source as a single translation unit, see
 * squash_unity.c/tools/self_verify.sh) silently exceeded 256 distinct
 * imports and hit this EXACT documented failure mode -- the resulting
 * binary compiled and linked with no error, then segfaulted on literally
 * "int main(){return 0;}", the "corrupting execution... at some arbitrary
 * *other* call site" symptom this comment already warned about. Also
 * see elf_grp_add's own new fprintf below: silently dropping the
 * (MAX_FUNCS+1)-th import produced a binary that LOOKED like it built
 * successfully -- there is no safe way to hit this cap silently, so it
 * no longer does. */
#define MAX_LIBS  8
#define MAX_FUNCS 2048
#define FUNC_NLEN 80
#define LIB_NLEN  64

/* Per-lib: name and func count.  512 = 8*64, 8 = MAX_LIBS */
static char elf_lib_names[512];  /* lib i name at offset i*64 */
static int  elf_lib_nfuncs[8];   /* number of funcs for lib i */
static int  elf_nlibs;

/* Per-func (indexed globally 0..total_funcs-1) */
static char elf_func_names[MAX_FUNCS * FUNC_NLEN]; /* func i name at offset i*80 */
static int  elf_func_lib[MAX_FUNCS];     /* which lib index func i belongs to */
static int  elf_func_got[MAX_FUNCS];     /* GOT slot index for func i */
static int  elf_total_funcs;

static void elf_grp_reset(void) { elf_nlibs = 0; elf_total_funcs = 0; }

static void elf_grp_add(const char *spec) {
    char lib[64];
    char func[80];
    const char *colon = spec;
    while (*colon && *colon != ':') colon++;
    if (!*colon) return;
    int liblen = (int)(colon - spec);
    if (liblen >= 63) liblen = 62;
    int ii = 0;
    while (ii < liblen) { lib[ii] = spec[ii]; ii++; }
    lib[ii] = '\0';
    /* skip non-Linux libraries: "extern:xxx" or Windows ".dll" names */
    if (strcmp(lib, "extern") == 0) return;
    { int ll = (int)strlen(lib);
      if (ll >= 4 && lib[ll-4]=='.' && lib[ll-3]=='d' && lib[ll-2]=='l' && lib[ll-1]=='l') return; }
    const char *fn = colon + 1;
    ii = 0;
    while (fn[ii] && ii < 79) { func[ii] = fn[ii]; ii++; }
    func[ii] = '\0';

    int lg = -1;
    int i = 0;
    while (i < elf_nlibs) {
        if (strcmp(elf_lib_names + i * 64, lib) == 0) { lg = i; break; }
        i++;
    }
    if (lg < 0) {
        if (elf_nlibs >= MAX_LIBS) return;
        lg = elf_nlibs++;
        ii = 0;
        while (lib[ii]) { elf_lib_names[lg * 64 + ii] = lib[ii]; ii++; }
        elf_lib_names[lg * 64 + ii] = '\0';
        elf_lib_nfuncs[lg] = 0;
    }
    /* check if func already registered for this lib */
    int fi = 0;
    while (fi < elf_total_funcs) {
        if (elf_func_lib[fi] == lg && strcmp(elf_func_names + fi * 80, func) == 0) return;
        fi++;
    }
    if (elf_total_funcs >= MAX_FUNCS) {
        /* A silently-dropped import here doesn't fail the build -- it
         * produces a binary that looks fine and corrupts execution at
         * some unrelated later call site instead (see this file's own
         * top comment). A loud, unmissable warning at the actual moment
         * of loss is the only safe behavior once the cap is ever hit. */
        fprintf(stderr, "squash: WARNING: exceeded MAX_FUNCS (%d) distinct dynamic imports -- "
                "'%s' from '%s' was NOT linked; the output binary WILL crash or behave "
                "incorrectly wherever it's called. Raise MAX_FUNCS in elf_builder.c.\n",
                MAX_FUNCS, func, lib);
        return;
    }
    int slot = elf_total_funcs;
    ii = 0;
    while (func[ii]) { elf_func_names[slot * 80 + ii] = func[ii]; ii++; }
    elf_func_names[slot * 80 + ii] = '\0';
    elf_func_lib[slot] = lg;
    elf_func_got[slot] = elf_total_funcs;
    elf_total_funcs++;
    elf_lib_nfuncs[lg]++;
}

static int elf_grp_got_idx(const char *fname) {
    int fi = 0;
    while (fi < elf_total_funcs) {
        if (strcmp(elf_func_names + fi * 80, fname) == 0)
            return elf_func_got[fi];
        fi++;
    }
    return -1;
}

static int elf_align_up(int v, int a) { return (v + a - 1) & ~(a - 1); }

/* Section-header name by index -- see the "shnames" comment at its call
 * site (elf_link_and_write's own section-header-table build step) for why
 * this is an explicit if-chain instead of a "static const char *arr[]"
 * lookup table. Indices must match elf_link_and_write's WRITE_SHDR call
 * order exactly (0=NULL, 1=.interp, ... 12=.shstrtab). */
static const char *elf_shdr_name(int i) {
    if (i==0) return "";
    if (i==1) return ".interp";
    if (i==2) return ".dynstr";
    if (i==3) return ".dynsym";
    if (i==4) return ".rela.dyn";
    if (i==5) return ".text";
    if (i==6) return ".rodata";
    if (i==7) return ".got";
    if (i==8) return ".dynamic";
    if (i==9) return ".data";
    if (i==10) return ".symtab";
    if (i==11) return ".strtab";
    if (i==12) return ".shstrtab";
    return "";
}

/* =========================================================================
 * elf_link_and_write
 * ========================================================================= */
int elf_link_and_write(ELFBuildInput *in) {
    int is64 = in->is_64bit;

    /* ---- Step 1: group imports ---- */
    elf_grp_reset();
    { char exit_key[128]; snprintf(exit_key,sizeof exit_key,"%s:exit",g_squash_libc_soname);
      elf_grp_add(exit_key); } /* always import exit so _start can call it to flush stdio */
    {
        int ii = 0;
        while (ii < in->import_count) { elf_grp_add(in->import_specs[ii]); ii++; }
    }

    /* Add extra DT_NEEDED libraries from linker -l flags.
     * They may not have any GOT entries (e.g. -lpthread on modern glibc
     * where symbols are already in libc.so.6), but DT_NEEDED is still needed. */
    {
        int li = 0;
        int nso = in->linker ? linker_soname_count(in->linker) : 0;
        while (li < nso) {
            const char *sn = linker_soname_get(in->linker, li);
            if (sn && sn[0]) {
                /* Inject a dummy entry to ensure the library appears in DT_NEEDED.
                 * We use a sentinel function name so elf_grp_add records the library. */
                char sentinel[256];
                int si = 0;
                while (sn[si] && si < 200) { sentinel[si] = sn[si]; si++; }
                sentinel[si] = '\0';
                /* Only add if not already present */
                int already = 0; int ei = 0;
                while (ei < elf_nlibs) {
                    if (strcmp(elf_lib_names + ei*64, sentinel) == 0) { already=1; break; }
                    ei++;
                }
                if (!already && elf_nlibs < MAX_LIBS) {
                    /* Add library with zero functions — appears in DT_NEEDED */
                    int lg = elf_nlibs++;
                    int k = 0;
                    while (sentinel[k] && k < 63) { elf_lib_names[lg*64+k]=sentinel[k]; k++; }
                    elf_lib_names[lg*64+k] = '\0';
                    elf_lib_nfuncs[lg] = 0;
                }
            }
            li++;
        }
    }
    int nfuncs = elf_total_funcs;

    /* ---- Step 2: build .dynstr ---- */
    int dynstr_cap = 1 + MAX_LIBS * LIB_NLEN + MAX_FUNCS * FUNC_NLEN + 8;
    uint8_t *dynstr = (uint8_t *)calloc(dynstr_cap, 1);
    int dynstr_len = 0;
    dynstr[dynstr_len++] = 0; /* index 0 = empty string */

    int lib_name_off[MAX_LIBS];
    {
        int li = 0;
        while (li < elf_nlibs) {
            lib_name_off[li] = dynstr_len;
            const char *s = elf_lib_names + li * 64;
            while (*s) dynstr[dynstr_len++] = (uint8_t)(*s++);
            dynstr[dynstr_len++] = 0;
            li++;
        }
    }

    int func_name_off[MAX_FUNCS];
    {
        int slot = 0;
        while (slot < nfuncs) {
            func_name_off[slot] = dynstr_len;
            const char *s = elf_func_names + slot * 80;
            while (*s) dynstr[dynstr_len++] = (uint8_t)(*s++);
            dynstr[dynstr_len++] = 0;
            slot++;
        }
    }
    int dynstr_sz = dynstr_len;

    /* ---- Step 3: _start stub ---- */
    uint8_t stub[48];
    int stub_len;
    int stub_call_off;
    int stub_exit_off; /* offset of 4-byte placeholder for call exit via GOT */

    if (in->is_arm64) {
        /* AArch64 _start:
         *   ldr  x0, [sp]        ; x0 = argc
         *   add  x1, sp, #8      ; x1 = argv
         *   bl   main            ; call main(argc, argv); result in w0
         *   movz x16, #lo16(exit_got_vma)
         *   movk x16, #hi16(exit_got_vma), lsl #16
         *   ldr  x16, [x16]      ; x16 = *exit_got_slot (populated by GLOB_DAT reloc)
         *   blr  x16             ; call exit(w0) — w0 untouched by the above
         * stub_call_off marks the whole BL word; stub_exit_off marks the MOVZ word
         * (MOVK immediately follows at stub_exit_off+4). */
        int si = 0;
        si = 0; e_pu32(stub,&si,0xF94003E0u); /* ldr x0,[sp] */
        e_pu32(stub,&si,0x910023E1u);         /* add x1,sp,#8 */
        stub_call_off = si;
        e_pu32(stub,&si,0x94000000u);         /* bl main (placeholder) */
        stub_exit_off = si;
        e_pu32(stub,&si,0xD2800010u);         /* movz x16,#0 */
        e_pu32(stub,&si,0xF2A00010u);         /* movk x16,#0,lsl#16 */
        e_pu32(stub,&si,0xF9400210u);         /* ldr x16,[x16] */
        e_pu32(stub,&si,0xD63F0200u);         /* blr x16 */
        stub_len = si;
    } else if (is64) {
        int si = 0;
        stub[si++]=0x31; stub[si++]=0xED; /* xor ebp,ebp */
        stub[si++]=0x5F;                   /* pop rdi */
        stub[si++]=0x48; stub[si++]=0x89; stub[si++]=0xE6; /* mov rsi,rsp */
        stub[si++]=0x48; stub[si++]=0x83; stub[si++]=0xE4; stub[si++]=0xF0; /* and rsp,-16 */
        stub[si++]=0xE8;
        stub_call_off = si;
        stub[si++]=0x00; stub[si++]=0x00; stub[si++]=0x00; stub[si++]=0x00; /* call main */
        stub[si++]=0x89; stub[si++]=0xC7; /* mov edi,eax */
        stub[si++]=0xFF; stub[si++]=0x15; /* call qword ptr [rip+disp32] -> libc exit() */
        stub_exit_off = si;
        stub[si++]=0x00; stub[si++]=0x00; stub[si++]=0x00; stub[si++]=0x00;
        stub_len = si;
    } else {
        int si = 0;
        stub[si++]=0x31; stub[si++]=0xED; /* xor ebp,ebp */
        stub[si++]=0x5E;                   /* pop esi */
        stub[si++]=0x89; stub[si++]=0xE1; /* mov ecx,esp */
        stub[si++]=0x83; stub[si++]=0xE4; stub[si++]=0xF0; /* and esp,-16 */
        /* Set x87 FPU precision to 64-bit double (PC=10) so that intermediate
         * x87 computations match C double semantics.  Without this, 1.2*10 in
         * 80-bit extended precision gives 11.999... which fistp truncates to 11
         * instead of 12.  CW=0x027F: PC=10, RC=00, all exceptions masked. */
        stub[si++]=0x68; stub[si++]=0x7F; stub[si++]=0x02; stub[si++]=0x00; stub[si++]=0x00; /* push 0x027F */
        stub[si++]=0xD9; stub[si++]=0x6C; stub[si++]=0x24; stub[si++]=0x00; /* fldcw [esp] */
        stub[si++]=0x83; stub[si++]=0xC4; stub[si++]=0x04; /* add esp, 4 */
        stub[si++]=0x51;                   /* push ecx */
        stub[si++]=0x56;                   /* push esi */
        stub[si++]=0xE8;
        stub_call_off = si;
        stub[si++]=0x00; stub[si++]=0x00; stub[si++]=0x00; stub[si++]=0x00; /* call main */
        stub[si++]=0x83; stub[si++]=0xC4; stub[si++]=0x08; /* add esp,8 */
        stub[si++]=0x50;                   /* push eax (exit code) */
        stub[si++]=0xFF; stub[si++]=0x15;  /* call dword ptr [exit_got_abs32] -> libc exit() */
        stub_exit_off = si;
        stub[si++]=0x00; stub[si++]=0x00; stub[si++]=0x00; stub[si++]=0x00;
        stub_len = si;
    }

    /* ---- Step 4: combine .text = stub + user code + static linked code ---- */
    int static_text_len = 0;
    uint8_t *static_text_bytes = NULL;
    if (in->linker) static_text_bytes = linker_static_text(in->linker, &static_text_len);
    int text_len = stub_len + in->text_len + static_text_len;
    uint8_t *text = (uint8_t *)malloc(text_len + 1);
    {
        int si = 0;
        while (si < stub_len) { text[si] = stub[si]; si++; }
        si = 0;
        while (si < in->text_len) { text[stub_len + si] = in->text[si]; si++; }
        si = 0;
        if (static_text_bytes) {
            while (si < static_text_len) { text[stub_len + in->text_len + si] = static_text_bytes[si]; si++; }
        }
    }

    /* Find main offset via __entry__ reloc */
    int main_user_off = 0;
    {
        int ri = 0;
        while (ri < in->reloc_count) {
            if (in->relocs[ri].symbol && strcmp(in->relocs[ri].symbol, "__entry__") == 0) {
                main_user_off = in->relocs[ri].addend; break;
            }
            ri++;
        }
    }
    int main_text_off = stub_len + main_user_off;

    /* Patch call disp32 in stub */
    if (in->is_arm64) {
        /* BL is PC-relative to its own address, offset in units of 4 bytes */
        int32_t imm26 = (int32_t)((main_text_off - stub_call_off) / 4);
        uint32_t instr = 0x94000000u | ((uint32_t)imm26 & 0x3FFFFFFu);
        e_patch32(text, stub_call_off, instr);
    } else {
        int disp = main_text_off - (stub_call_off + 4);
        text[stub_call_off+0] = (uint8_t)(disp);
        text[stub_call_off+1] = (uint8_t)(disp>>8);
        text[stub_call_off+2] = (uint8_t)(disp>>16);
        text[stub_call_off+3] = (uint8_t)(disp>>24);
    }

    /* ---- Step 5: layout ---- */
    uint64_t load_base = is64 ? (uint64_t)0x400000ULL : (uint64_t)0x08048000ULL;
    /* PT_LOAD alignment/padding unit, also written out as each PT_LOAD's
     * p_align. x86 Linux is universally 4K-paged, but arm64 Linux is not:
     * distros/kernels are free to run 4K, 16K, or 64K pages (e.g. this
     * project's own real arm64 test host runs a 16K-page kernel), and the
     * kernel's ELF loader requires p_vaddr/p_offset to already be aligned
     * to (at least) the actual runtime page size or it maps the segment
     * wrong and the very first instruction fetch at the entry point faults
     * with SEGV_ACCERR before main() ever runs (confirmed exactly this way
     * on the 16K-page host — every arm64 Linux binary this compiler
     * produced was crashing immediately on entry). 64K is the largest page
     * size any real Linux/arm64 config uses, so aligning to it up front
     * keeps every arm64 output correct regardless of the target kernel's
     * actual page size, at the cost of a little extra padding in the
     * output file. */
    int page = in->is_arm64 ? 65536 : 4096;
    int got_slot_size  = is64 ? 8 : 4;
    int sym_entry_size = is64 ? 24 : 16;
    int rela_entry_size = is64 ? 24 : 8;
    int dyn_entry_size  = is64 ? 16 : 8;
    int ehdr_size = is64 ? 64 : 52;
    int phdr_size = is64 ? 56 : 32;
    /* OpenBSD needs one extra PT_NOTE program header carrying
     * .note.openbsd.ident — see the branding note below. */
    int n_phdrs = in->is_openbsd ? 7 : 6;

    const char *interp_str;
    if (in->is_openbsd)    interp_str = "/usr/libexec/ld.so";
    else if (in->is_arm64) interp_str = "/lib/ld-linux-aarch64.so.1";
    else if (is64)         interp_str = "/lib64/ld-linux-x86-64.so.2";
    else                   interp_str = "/lib/ld-linux.so.2";
    int interp_len = 0;
    while (interp_str[interp_len]) interp_len++;
    interp_len++;

    /* OpenBSD ELF branding: the kernel's exec_elf will refuse ENOEXEC on a
     * native binary unless it carries a PT_NOTE segment with an
     * ".note.openbsd.ident" note (name="OpenBSD", type=NT_OPENBSD_IDENT=1,
     * 4-byte desc — conventionally 0). Unlike EI_OSABI (which some loaders
     * treat as advisory), this note is load-bearing: without it the kernel
     * cannot tell this apart from a Linux/FreeBSD ELF binary and won't run
     * it natively at all. Layout: namesz(4) descsz(4) type(4) name(8,
     * "OpenBSD\0" is already 4-byte aligned) desc(4) = 24 bytes total. */
    uint8_t note_buf[24];
    int note_len = 0;
    if (in->is_openbsd) {
        int no = 0;
        e_pu32(note_buf,&no,8);  /* namesz */
        e_pu32(note_buf,&no,4);  /* descsz */
        e_pu32(note_buf,&no,1);  /* type = NT_OPENBSD_IDENT */
        const char *nm = "OpenBSD"; int ni = 0;
        while (nm[ni]) { e_pu8(note_buf,&no,(uint8_t)nm[ni]); ni++; }
        e_pu8(note_buf,&no,0); /* NUL terminator, completes the 8-byte name */
        e_pu32(note_buf,&no,0); /* desc: OS version, 0 = unspecified/any */
        note_len = no;
    }

    int dynsym_count = 1 + nfuncs;
    int dynsym_sz = dynsym_count * sym_entry_size;
    int relasz = nfuncs * rela_entry_size;
    int got_sz = nfuncs * got_slot_size;
    int n_dyn = elf_nlibs + 9 + 1;
    int dynamic_sz = n_dyn * dyn_entry_size;
    int static_rdata_len = 0;
    uint8_t *static_rdata_bytes = NULL;
    if (in->linker) static_rdata_bytes = linker_static_rdata(in->linker, &static_rdata_len);
    int rodata_sz = in->rdata_strings_len + static_rdata_len;
    int data_sz   = in->wdata_len;

    int off_ehdr  = 0;
    int off_phdrs = ehdr_size;
    int headers_end = ehdr_size + n_phdrs * phdr_size;
    int off_note   = headers_end;
    int off_interp = off_note + note_len;
    int off_dynstr = off_interp + interp_len;
    int off_dynsym = elf_align_up(off_dynstr + dynstr_sz, 8);
    int off_reloc  = elf_align_up(off_dynsym + dynsym_sz, 8);
    int off_text   = elf_align_up(off_reloc  + relasz, 16);
    int off_rodata = off_text + text_len;
    int rx_seg_filesz = off_rodata + rodata_sz;
    int rx_seg_filesz_padded = elf_align_up(rx_seg_filesz, page);

    int off_got     = rx_seg_filesz_padded;
    int off_dynamic = elf_align_up(off_got + got_sz, 8);
    int off_data    = elf_align_up(off_dynamic + dynamic_sz, 8);
    int rw_seg_filesz = off_data + data_sz;

    uint64_t vma_rx     = load_base;
    uint64_t vma_note   = vma_rx + (uint64_t)off_note;
    uint64_t vma_interp = vma_rx + (uint64_t)off_interp;
    uint64_t vma_dynstr = vma_rx + (uint64_t)off_dynstr;
    uint64_t vma_dynsym = vma_rx + (uint64_t)off_dynsym;
    uint64_t vma_reloc  = vma_rx + (uint64_t)off_reloc;
    uint64_t vma_text   = vma_rx + (uint64_t)off_text;
    uint64_t vma_rodata = vma_rx + (uint64_t)off_rodata;
    uint64_t vma_rw     = load_base + (uint64_t)rx_seg_filesz_padded;
    uint64_t vma_got    = vma_rw + (uint64_t)(off_got - rx_seg_filesz_padded);
    uint64_t vma_dynamic = vma_rw + (uint64_t)(off_dynamic - rx_seg_filesz_padded);
    uint64_t vma_data   = vma_rw + (uint64_t)(off_data - rx_seg_filesz_padded);
    uint64_t entry_vma  = vma_text;

    /* ---- Step 5.5: patch stub's call-exit displacement ---- */
    {
        int exit_slot = elf_grp_got_idx("exit");
        if (exit_slot >= 0) {
            uint64_t got_vma = vma_got + (uint64_t)(exit_slot * got_slot_size);
            if (in->is_arm64) {
                /* Re-encode the MOVZ/MOVK pair with the absolute GOT slot address
                 * (load_base keeps this well under 32 bits, so two 16-bit halves suffice). */
                uint32_t lo16 = (uint32_t)(got_vma & 0xFFFFu);
                uint32_t hi16 = (uint32_t)((got_vma >> 16) & 0xFFFFu);
                uint32_t movz = 0xD2800010u | (lo16 << 5); /* movz x16,#lo16 */
                uint32_t movk = 0xF2A00010u | (hi16 << 5); /* movk x16,#hi16,lsl#16 */
                e_patch32(text, stub_exit_off,   movz);
                e_patch32(text, stub_exit_off+4, movk);
            } else if (is64) {
                uint64_t patch_vma = vma_text + (uint64_t)stub_exit_off;
                int64_t disp = (int64_t)(got_vma - (patch_vma + 4));
                text[stub_exit_off+0]=(uint8_t)(disp);     text[stub_exit_off+1]=(uint8_t)(disp>>8);
                text[stub_exit_off+2]=(uint8_t)(disp>>16); text[stub_exit_off+3]=(uint8_t)(disp>>24);
            } else {
                uint32_t abs_va = (uint32_t)got_vma;
                text[stub_exit_off+0]=(uint8_t)(abs_va);     text[stub_exit_off+1]=(uint8_t)(abs_va>>8);
                text[stub_exit_off+2]=(uint8_t)(abs_va>>16); text[stub_exit_off+3]=(uint8_t)(abs_va>>24);
            }
        }
    }

    /* ---- Step 6: .dynsym ---- */
    uint8_t *dynsym_buf = (uint8_t *)calloc(dynsym_sz + 1, 1);
    {
        int doff = 0;
        int zi = 0; while (zi < sym_entry_size) { dynsym_buf[doff++] = 0; zi++; }
        int slot = 0;
        while (slot < nfuncs) {
            if (is64) {
                e_pu32(dynsym_buf, &doff, (uint32_t)func_name_off[slot]);
                e_pu8 (dynsym_buf, &doff, 0x12); /* STB_GLOBAL|STT_FUNC */
                e_pu8 (dynsym_buf, &doff, 0x00);
                e_pu16(dynsym_buf, &doff, 0x00);
                e_pu64(dynsym_buf, &doff, 0);
                e_pu64(dynsym_buf, &doff, 0);
            } else {
                e_pu32(dynsym_buf, &doff, (uint32_t)func_name_off[slot]);
                e_pu32(dynsym_buf, &doff, 0);
                e_pu32(dynsym_buf, &doff, 0);
                e_pu8 (dynsym_buf, &doff, 0x12);
                e_pu8 (dynsym_buf, &doff, 0x00);
                e_pu16(dynsym_buf, &doff, 0x00);
            }
            slot++;
        }
    }

    /* ---- Step 7: .rela.dyn / .rel.dyn ---- */
    uint8_t *reloc_buf = (uint8_t *)calloc(relasz + 1, 1);
    {
        int roff = 0;
        int slot = 0;
        while (slot < nfuncs) {
            uint64_t got_slot_vma = vma_got + (uint64_t)(slot * got_slot_size);
            int sym_idx = slot + 1;
            if (is64) {
                /* r_info = (sym_idx << 32) | reloc_type; use hl to avoid 64-bit shift.
                 * type 6 = R_X86_64_GLOB_DAT, type 1025 = R_AARCH64_GLOB_DAT */
                uint32_t glob_dat_type = in->is_arm64 ? 1025u : 6u;
                e_pu64(reloc_buf, &roff, got_slot_vma);
                e_pu64_hl(reloc_buf, &roff, (uint32_t)sym_idx, glob_dat_type); /* hi=sym_idx, lo=type */
                e_pu64(reloc_buf, &roff, 0);
            } else {
                uint32_t r_info = ((uint32_t)sym_idx << 8) | 6; /* R_386_GLOB_DAT */
                e_pu32(reloc_buf, &roff, (uint32_t)got_slot_vma);
                e_pu32(reloc_buf, &roff, r_info);
            }
            slot++;
        }
    }

    /* ---- Step 8: .dynamic ---- */
    uint8_t *dynamic_buf = (uint8_t *)calloc(dynamic_sz + 1, 1);
    {
        int doff = 0;
        int li2 = 0;
        while (li2 < elf_nlibs) {
            if (is64) {
                e_pu64(dynamic_buf, &doff, 1);
                e_pu64(dynamic_buf, &doff, (uint64_t)lib_name_off[li2]);
            } else {
                e_pu32(dynamic_buf, &doff, 1);
                e_pu32(dynamic_buf, &doff, (uint32_t)lib_name_off[li2]);
            }
            li2++;
        }
        if (is64) {
            e_pu64(dynamic_buf,&doff,5);  e_pu64(dynamic_buf,&doff,vma_dynstr);
            e_pu64(dynamic_buf,&doff,10); e_pu64(dynamic_buf,&doff,(uint64_t)dynstr_sz);
            e_pu64(dynamic_buf,&doff,6);  e_pu64(dynamic_buf,&doff,vma_dynsym);
            e_pu64(dynamic_buf,&doff,11); e_pu64(dynamic_buf,&doff,(uint64_t)sym_entry_size);
            e_pu64(dynamic_buf,&doff,7);  e_pu64(dynamic_buf,&doff,vma_reloc);
            e_pu64(dynamic_buf,&doff,8);  e_pu64(dynamic_buf,&doff,(uint64_t)relasz);
            e_pu64(dynamic_buf,&doff,9);  e_pu64(dynamic_buf,&doff,(uint64_t)rela_entry_size);
            e_pu64(dynamic_buf,&doff,30); e_pu64(dynamic_buf,&doff,8); /* DF_BIND_NOW */
            e_pu64(dynamic_buf,&doff,0);  e_pu64(dynamic_buf,&doff,0); /* DT_NULL */
        } else {
            e_pu32(dynamic_buf,&doff,5);  e_pu32(dynamic_buf,&doff,(uint32_t)vma_dynstr);
            e_pu32(dynamic_buf,&doff,10); e_pu32(dynamic_buf,&doff,(uint32_t)dynstr_sz);
            e_pu32(dynamic_buf,&doff,6);  e_pu32(dynamic_buf,&doff,(uint32_t)vma_dynsym);
            e_pu32(dynamic_buf,&doff,11); e_pu32(dynamic_buf,&doff,(uint32_t)sym_entry_size);
            e_pu32(dynamic_buf,&doff,17); e_pu32(dynamic_buf,&doff,(uint32_t)vma_reloc);
            e_pu32(dynamic_buf,&doff,18); e_pu32(dynamic_buf,&doff,(uint32_t)relasz);
            e_pu32(dynamic_buf,&doff,19); e_pu32(dynamic_buf,&doff,(uint32_t)rela_entry_size);
            e_pu32(dynamic_buf,&doff,30); e_pu32(dynamic_buf,&doff,8);
            e_pu32(dynamic_buf,&doff,0);  e_pu32(dynamic_buf,&doff,0);
        }
    }

    /* ---- Step 9: patch relocations in .text ---- */
    {
        int ri = 0;
        while (ri < in->reloc_count) {
            Relocation *r = &in->relocs[ri];
            if (!r->symbol) { ri++; continue; }
            if (strcmp(r->symbol, "__entry__") == 0) { ri++; continue; }

            int patch_off = r->offset + stub_len;

            if (r->kind == RELOC_A64_IAT_ADRP || r->kind == RELOC_A64_IAT_LO12) {
                int slot = elf_grp_got_idx(r->symbol);
                if (slot < 0) { ri++; continue; }
                uint64_t target_vma = vma_got + (uint64_t)(slot * got_slot_size);
                if (r->kind == RELOC_A64_IAT_ADRP) {
                    uint64_t pc_vma = vma_text + (uint64_t)patch_off;
                    e_patch_a64_adrp(text, patch_off, target_vma, pc_vma);
                } else {
                    e_patch_a64_ldr_lo12(text, patch_off, target_vma);
                }

            } else if (r->kind == RELOC_A64_DATA_ADRP || r->kind == RELOC_A64_DATA_LO12) {
                int slab_off = -1;
                int si = 0;
                while (si < in->string_count) {
                    if (strcmp(in->string_labels[si], r->symbol) == 0) { slab_off = in->string_offsets[si]; break; }
                    si++;
                }
                if (slab_off < 0) { ri++; continue; }
                uint64_t target_vma = vma_rodata + (uint64_t)slab_off;
                if (r->kind == RELOC_A64_DATA_ADRP) {
                    uint64_t pc_vma = vma_text + (uint64_t)patch_off;
                    e_patch_a64_adrp(text, patch_off, target_vma, pc_vma);
                } else {
                    e_patch_a64_add_lo12(text, patch_off, target_vma);
                }

            } else if (r->kind == RELOC_A64_WDATA_ADRP || r->kind == RELOC_A64_WDATA_LO12) {
                int wlab_off = -1;
                int wi = 0;
                while (wi < in->wdata_count) {
                    if (in->wdata_labels[wi] && strcmp(in->wdata_labels[wi], r->symbol) == 0) { wlab_off = in->wdata_offsets[wi]; break; }
                    wi++;
                }
                if (wlab_off < 0) { ri++; continue; }
                uint64_t target_vma = vma_data + (uint64_t)wlab_off;
                if (r->kind == RELOC_A64_WDATA_ADRP) {
                    uint64_t pc_vma = vma_text + (uint64_t)patch_off;
                    e_patch_a64_adrp(text, patch_off, target_vma, pc_vma);
                } else {
                    e_patch_a64_add_lo12(text, patch_off, target_vma);
                }

            } else if (r->kind == RELOC_IAT_REL32) {
                int slot = elf_grp_got_idx(r->symbol);
                if (slot < 0) { ri++; continue; }
                uint64_t got_slot_vma = vma_got + (uint64_t)(slot * got_slot_size);
                uint64_t patch_vma = vma_text + (uint64_t)(r->offset + stub_len);
                int64_t disp = (int64_t)(got_slot_vma - (patch_vma + 4));
                e_patch32(text, patch_off, (uint32_t)(int32_t)disp);

            } else if (r->kind == RELOC_ABS32) {
                int slot = elf_grp_got_idx(r->symbol);
                if (slot < 0) { ri++; continue; }
                uint32_t abs_va = (uint32_t)(vma_got + (uint64_t)(slot * got_slot_size));
                e_patch32(text, patch_off, abs_va);

            } else if (r->kind == RELOC_DATA_REL32) {
                int slab_off = -1;
                int si = 0;
                while (si < in->string_count) {
                    if (strcmp(in->string_labels[si], r->symbol) == 0) { slab_off = in->string_offsets[si]; break; }
                    si++;
                }
                if (slab_off < 0) { ri++; continue; }
                uint64_t sym_vma = vma_rodata + (uint64_t)slab_off;
                uint64_t patch_vma = vma_text + (uint64_t)(r->offset + stub_len);
                int64_t disp = (int64_t)(sym_vma - (patch_vma + 4));
                e_patch32(text, patch_off, (uint32_t)(int32_t)disp);

            } else if (r->kind == RELOC_DATA_ABS32) {
                int slab_off = -1;
                int si = 0;
                while (si < in->string_count) {
                    if (strcmp(in->string_labels[si], r->symbol) == 0) { slab_off = in->string_offsets[si]; break; }
                    si++;
                }
                if (slab_off < 0) { ri++; continue; }
                uint32_t abs_va = (uint32_t)(vma_rodata + (uint64_t)slab_off);
                e_patch32(text, patch_off, abs_va);

            } else if (r->kind == RELOC_WDATA_REL32) {
                int wlab_off = -1;
                int wi = 0;
                while (wi < in->wdata_count) {
                    if (in->wdata_labels[wi] && strcmp(in->wdata_labels[wi], r->symbol) == 0) { wlab_off = in->wdata_offsets[wi]; break; }
                    wi++;
                }
                if (wlab_off < 0) { ri++; continue; }
                uint64_t sym_vma = vma_data + (uint64_t)wlab_off;
                uint64_t patch_vma = vma_text + (uint64_t)(r->offset + stub_len);
                int64_t disp = (int64_t)(sym_vma - (patch_vma + 4));
                e_patch32(text, patch_off, (uint32_t)(int32_t)disp);

            } else if (r->kind == RELOC_WDATA_ABS32) {
                int wlab_off = -1;
                int wi = 0;
                while (wi < in->wdata_count) {
                    if (in->wdata_labels[wi] && strcmp(in->wdata_labels[wi], r->symbol) == 0) { wlab_off = in->wdata_offsets[wi]; break; }
                    wi++;
                }
                if (wlab_off < 0) { ri++; continue; }
                uint32_t abs_va = (uint32_t)(vma_data + (uint64_t)wlab_off);
                e_patch32(text, patch_off, abs_va);

            } else if (r->kind == RELOC_STATIC_REL32) {
                /* PC-relative call to a statically-linked function */
                if (!in->linker) { ri++; continue; }
                int fi = 0;
                int nsd = linker_static_def_count(in->linker);
                int found_sd = 0;
                while (fi < nsd) {
                    if (strcmp(linker_static_def_name(in->linker, fi), r->symbol) == 0) {
                        int sym_off  = linker_static_def_off(in->linker, fi);
                        uint64_t sym_vma  = vma_text + (uint64_t)(stub_len + in->text_len + sym_off);
                        uint64_t patch_vma = vma_text + (uint64_t)(r->offset + stub_len);
                        int32_t disp = (int32_t)((int64_t)(sym_vma - (patch_vma + 4)));
                        e_patch32(text, patch_off, (uint32_t)disp);
                        found_sd = 1;
                        break;
                    }
                    fi++;
                }
                if (!found_sd) {
                    diag_emit(DIAG_ERROR, -1, NULL, NULL,
                              "unresolved static symbol: %s", r->symbol);
                }

            } else if (r->kind == RELOC_A64_STATIC_BL) {
                /* AArch64 sibling of RELOC_STATIC_REL32 above: a direct BL
                 * to a function pulled in from a ".a" static archive (see
                 * linker_link_static()/linker_has_static_def() in
                 * codegen_arm64.c's a64_emit_call). This only handles the
                 * ".a"-supplied case — a cross-".sqo"-object call is instead
                 * resolved earlier and unconditionally by objfile_merge()
                 * itself (see its own RELOC_A64_STATIC_BL case), which
                 * patches the BL immediate directly into the merged text
                 * and drops the relocation, so it never reaches here. */
                if (!in->linker) { ri++; continue; }
                int fi = 0;
                int nsd = linker_static_def_count(in->linker);
                int found_sd = 0;
                while (fi < nsd) {
                    if (strcmp(linker_static_def_name(in->linker, fi), r->symbol) == 0) {
                        int sym_off  = linker_static_def_off(in->linker, fi);
                        uint64_t sym_vma  = vma_text + (uint64_t)(stub_len + in->text_len + sym_off);
                        uint64_t patch_vma = vma_text + (uint64_t)(r->offset + stub_len);
                        e_patch_a64_call26(text, patch_off, sym_vma, patch_vma);
                        found_sd = 1;
                        break;
                    }
                    fi++;
                }
                if (!found_sd) {
                    diag_emit(DIAG_ERROR, -1, NULL, NULL,
                              "unresolved static symbol: %s", r->symbol);
                }

            } else if (r->kind == RELOC_TEXT_ABS32 && !is64 && in->as_) {
                /* 32-bit function pointer address: "__lbl_NNN" → abs VA of that label */
                if (r->symbol && strncmp(r->symbol, "__lbl_", 6) == 0) {
                    int label_id = 0;
                    { const char *p = r->symbol + 6; while (*p >= '0' && *p <= '9') { label_id = label_id*10 + (*p - '0'); p++; } }
                    if (label_id >= 0 && label_id < in->as_->label_count) {
                        int lbl_off = in->as_->labels[label_id].offset;
                        if (lbl_off >= 0) {
                            uint32_t abs_va = (uint32_t)(vma_text + (uint64_t)stub_len + (uint64_t)lbl_off);
                            e_patch32(text, patch_off, abs_va);
                        }
                    }
                }
            }
            ri++;
        }
    }

    /* ---- Step 9.5: resolve static-text internal relocations ---- */
    if (in->linker) {
        int sreloc_count = 0;
        StaticReloc *sr = linker_static_relocs(in->linker, &sreloc_count);
        int sri;
        for (sri = 0; sri < sreloc_count; sri++) {
            StaticReloc *s = &sr[sri];
            int global_off = stub_len + in->text_len + s->offset;
            if (global_off < 0 || global_off + 4 > text_len) continue;

            if (in->is_arm64) {
                if (s->type == 3) { /* CALL26/JUMP26 — direct BL/B */
                    int fi = 0; int nsd = linker_static_def_count(in->linker); int found=0;
                    while (fi < nsd) {
                        if (strcmp(linker_static_def_name(in->linker,fi), s->sym)==0) {
                            int sym_off = linker_static_def_off(in->linker,fi);
                            uint64_t sym_vma   = vma_text+(uint64_t)(stub_len+in->text_len+sym_off);
                            uint64_t patch_vma = vma_text+(uint64_t)global_off;
                            e_patch_a64_call26(text, global_off, sym_vma + (uint64_t)s->addend, patch_vma);
                            found=1; break;
                        }
                        fi++;
                    }
                    if (!found && in->as_) {
                        /* Try user code function labels (no AArch64
                         * equivalent of x86-64's "rewrite to indirect GOT
                         * call" trick exists here — a BL's 26-bit immediate
                         * can only reach a direct branch target, never a
                         * GOT slot; a genuinely dynamic/import target would
                         * need codegen to have emitted ADRP+LDR+BLR instead,
                         * which never produces a CALL26 relocation). */
                        int li2;
                        for (li2 = 0; li2 < in->as_->label_count; li2++) {
                            if (in->as_->labels[li2].name &&
                                strcmp(in->as_->labels[li2].name, s->sym) == 0 &&
                                in->as_->labels[li2].offset >= 0) {
                                int lbl_off = in->as_->labels[li2].offset;
                                uint64_t sym_vma   = vma_text+(uint64_t)(stub_len+lbl_off);
                                uint64_t patch_vma = vma_text+(uint64_t)global_off;
                                e_patch_a64_call26(text, global_off, sym_vma + (uint64_t)s->addend, patch_vma);
                                found = 1; break;
                            }
                        }
                    }
                    if (!found)
                        diag_emit(DIAG_ERROR, -1, NULL, NULL, "unresolved static reloc CALL26 to '%s'", s->sym);
                } else if (s->type == 4 || s->type == 5 || s->type == 6 || s->type == 7) {
                    /* ADRP / ADD_LO12 / LDST64_LO12 / ABS64 all resolve
                     * against static rodata — the common case for code
                     * pulled from a real .a archive member: a reference to
                     * a string or const-data symbol also defined in that
                     * member (see linker_static_rdata_*, populated by
                     * link_elf_object()'s .rodata pass). */
                    if (s->type == 7 && global_off + 8 > text_len) continue;
                    int fi=0; int nrd=linker_static_rdata_count(in->linker); int found=0;
                    while (fi < nrd) {
                        if (strcmp(linker_static_rdata_name(in->linker,fi), s->sym)==0) {
                            int rd_off = linker_static_rdata_off(in->linker, fi);
                            uint64_t sym_vma = vma_rodata + (uint64_t)(in->rdata_strings_len + rd_off) + (uint64_t)s->addend;
                            uint64_t patch_vma = vma_text + (uint64_t)global_off;
                            if (s->type == 4)      e_patch_a64_adrp(text, global_off, sym_vma, patch_vma);
                            else if (s->type == 5) e_patch_a64_add_lo12(text, global_off, sym_vma);
                            else if (s->type == 6) e_patch_a64_ldr_lo12(text, global_off, sym_vma);
                            else                   e_patch64(text, global_off, sym_vma);
                            found=1; break;
                        }
                        fi++;
                    }
                    if (!found)
                        diag_emit(DIAG_ERROR, -1, NULL, NULL, "unresolved static reloc (arm64 type %d) to '%s'", s->type, s->sym);
                }
            } else if (s->type == 0) { /* PC32/PLT32 */
                /* Check static definitions */
                int fi = 0; int nsd = linker_static_def_count(in->linker); int found=0;
                while (fi < nsd) {
                    if (strcmp(linker_static_def_name(in->linker,fi), s->sym)==0) {
                        int sym_off = linker_static_def_off(in->linker,fi);
                        uint64_t sym_vma   = vma_text+(uint64_t)(stub_len+in->text_len+sym_off);
                        uint64_t patch_vma = vma_text+(uint64_t)global_off;
                        int32_t disp=(int32_t)((int64_t)(sym_vma+s->addend-(patch_vma+4)));
                        e_patch32(text, global_off, (uint32_t)disp);
                        found=1; break;
                    }
                    fi++;
                }
                if (!found) {
                    /* Try as a GOT import (dynamic function) */
                    int slot = elf_grp_got_idx(s->sym);
                    if (slot >= 0) {
                        uint64_t got_vma = vma_got + (uint64_t)(slot * got_slot_size);
                        if (is64) {
                            /* Change preceding opcode byte from E8 (call rel32) to
                             * FF 15 (call [rip+rel32]) by patching bytes at global_off-1..+4 */
                            if (global_off >= 1 && text[global_off-1] == 0xE8) {
                                text[global_off-1] = 0x15; /* will be prefixed with FF */
                                /* shift: insert 0xFF before 0x15 — but that needs a byte insert.
                                 * Simpler: emit a trampoline at end of static_text instead. */
                                /* For now: use a different approach — emit nop + call indirect */
                                /* Actually: patch as if it's already FF 15 by adjusting offset */
                                text[global_off-1] = 0xFF;
                                /* We need to insert 0x15 — but global_off-1 is where 0xE8 was */
                                /* Rewrite: we use the fact that the linker emits E8 with SRELOC,
                                 * and changes it to FF 15 before the 4-byte slot. But we're at
                                 * the 4-byte slot (global_off), and opcode is at global_off-1.
                                 * E8 is 1 byte; FF 15 is 2 bytes — we'd need to shift everything.
                                 * Instead: keep E8 but call a trampoline. Add trampoline to text. */
                                text[global_off-1] = 0xE8; /* restore E8 */
                                /* Compute trampoline offset in static_text.
                                 * We'll append a trampoline for this slot just past static_text. */
                                /* For simplicity, route through GOT via a small thunk appended
                                 * to the static text region. Mark it as a RELOC_IAT_REL32 instead. */
                                /* Patch the call to point to the GOT indirection stub.
                                 * We will patch it to call via [rip+disp] anyway — but the
                                 * instruction byte E8 means direct call. We must change to FF 15.
                                 * Since we can't insert bytes, we leave E8 and make it call a stub. */
                                uint64_t patch_vma = vma_text + (uint64_t)global_off;
                                int64_t  disp      = (int64_t)(got_vma - (patch_vma + 4));
                                /* This is wrong for E8 (direct call to GOT address, not indirection),
                                 * but we can't easily fix this without inserting bytes.
                                 * The correct solution is to change E8→FF 15 by rewriting 2 bytes.
                                 * Since E8 is 1-byte opcode at global_off-1, and we need 2-byte FF 15,
                                 * we need 1 more byte. We'll steal the byte at global_off-2 if it's a
                                 * NOP (0x90), otherwise emit a JMP stub. */
                                if (global_off >= 2 && text[global_off-2] == 0x90) {
                                    text[global_off-2] = 0xFF;
                                    text[global_off-1] = 0x15;
                                    e_patch32(text, global_off, (uint32_t)(int32_t)((int64_t)(got_vma-(patch_vma+4))));
                                } else {
                                    /* Can't easily rewrite — just use direct call to GOT address value.
                                     * This won't work at runtime. Print a warning. */
                                    diag_emit(DIAG_WARNING, -1, NULL, NULL, "cannot rewrite call to %s as indirect GOT call", s->sym);
                                }
                            }
                        } else {
                            /* 32-bit: call dword ptr [abs32] — change E8 to FF 15 */
                            if (global_off >= 1 && text[global_off-1] == 0xE8) {
                                uint32_t abs_va = (uint32_t)got_vma;
                                if (global_off >= 2 && text[global_off-2] == 0x90) {
                                    text[global_off-2] = 0xFF;
                                    text[global_off-1] = 0x15;
                                    e_patch32(text, global_off, abs_va);
                                } else {
                                    diag_emit(DIAG_WARNING, -1, NULL, NULL, "cannot rewrite 32-bit call to %s", s->sym);
                                }
                            }
                        }
                        found = 1;
                    }
                }
                if (!found) {
                    /* Try user code function labels */
                    if (in->as_) {
                        int li2;
                        for (li2 = 0; li2 < in->as_->label_count; li2++) {
                            if (in->as_->labels[li2].name &&
                                strcmp(in->as_->labels[li2].name, s->sym) == 0 &&
                                in->as_->labels[li2].offset >= 0) {
                                int lbl_off = in->as_->labels[li2].offset;
                                uint64_t sym_vma   = vma_text+(uint64_t)(stub_len+lbl_off);
                                uint64_t patch_vma = vma_text+(uint64_t)global_off;
                                int32_t disp=(int32_t)((int64_t)(sym_vma+s->addend-(patch_vma+4)));
                                e_patch32(text, global_off, (uint32_t)disp);
                                found = 1; break;
                            }
                        }
                    }
                }
                if (!found)
                    diag_emit(DIAG_ERROR, -1, NULL, NULL, "unresolved static reloc PC32 to '%s'", s->sym);

            } else if (s->type == 1) { /* ABS32 */
                /* Check static rodata */
                int fi=0; int nrd=linker_static_rdata_count(in->linker); int found=0;
                while (fi < nrd) {
                    if (strcmp(linker_static_rdata_name(in->linker,fi), s->sym)==0) {
                        int rd_off = linker_static_rdata_off(in->linker, fi);
                        /* offset within combined rodata = user_rodata + rd_off */
                        uint64_t sym_vma = vma_rodata + (uint64_t)(in->rdata_strings_len + rd_off);
                        if (is64) {
                            uint64_t patch_vma = vma_text+(uint64_t)global_off;
                            int32_t disp=(int32_t)((int64_t)(sym_vma+s->addend-(patch_vma+4)));
                            e_patch32(text, global_off, (uint32_t)disp);
                        } else {
                            uint32_t abs_va = (uint32_t)(sym_vma + s->addend);
                            e_patch32(text, global_off, abs_va);
                        }
                        found=1; break;
                    }
                    fi++;
                }
                if (!found)
                    diag_emit(DIAG_ERROR, -1, NULL, NULL, "unresolved static reloc ABS32 to '%s'", s->sym);
            }
        }
    }

    /* ---- Step 9.6: section headers (.symtab/.strtab/.shstrtab + a real
     * SHT describing every section this file already contains) ----
     * Historically this writer produced zero section headers at all
     * (e_shoff/e_shnum/e_shstrndx all left as 0) -- valid per the ELF spec
     * (section headers are optional at RUNTIME, the kernel loader and
     * dynamic linker only ever look at program headers), but it meant
     * readelf/objdump/nm/gdb couldn't make sense of these binaries at all
     * (gdb outright refused to recognize the file format), and valgrind
     * could only ever report raw, unsymbolized addresses. Every section
     * added here describes a byte range that ALREADY exists in the file
     * (nothing about the actual program image changes) except for three
     * brand new, non-loaded (no SHF_ALLOC) sections appended past the end
     * of the file's last real segment: .symtab, .strtab, .shstrtab. Being
     * non-loaded, they add zero runtime cost (the kernel never maps them)
     * and cannot affect program behavior -- purely descriptive metadata for
     * tools. .symtab is built from every label the assembler ever defined
     * (in->as_->labels[]) -- this includes real function names (via
     * get_func_label(), which names the label after the C function) AND
     * internal branch-target labels (loop/if labels etc, synthetically
     * named) side by side, exactly like an unoptimized "-g -O0" compile's
     * own symtab tends to be noisy with local labels -- harmless for tools:
     * gdb/addr2line just resolve to "nearest preceding symbol" either way,
     * so a stray internal label at worst gives a slightly-off-but-still-
     * useful name instead of a bare hex address. */
    int n_syms = 0;
    if (in->as_) {
        int li; for (li = 0; li < in->as_->label_count; li++)
            if (in->as_->labels[li].name && in->as_->labels[li].offset >= 0) n_syms++;
    }
    int symtab_count = 1 + n_syms; /* +1 for the mandatory null entry at index 0 */
    int symtab_sz = symtab_count * sym_entry_size;

    uint8_t *symtab_buf = (uint8_t *)calloc((size_t)symtab_sz + 1, 1);
    /* strtab: index 0 is always the empty string (required by the ELF spec) */
    int strtab_cap = 1;
    if (in->as_) {
        int li; for (li = 0; li < in->as_->label_count; li++)
            if (in->as_->labels[li].name && in->as_->labels[li].offset >= 0)
                strtab_cap += (int)strlen(in->as_->labels[li].name) + 1;
    }
    uint8_t *strtab_buf = (uint8_t *)calloc((size_t)strtab_cap + 1, 1);
    int strtab_len = 0;
    strtab_buf[strtab_len++] = 0;
    {
        int soff = 0;
        int zi = 0; while (zi < sym_entry_size) { symtab_buf[soff++] = 0; zi++; } /* index 0 = null symbol */
        if (in->as_) {
            int li;
            for (li = 0; li < in->as_->label_count; li++) {
                if (!in->as_->labels[li].name || in->as_->labels[li].offset < 0) continue;
                int name_off = strtab_len;
                const char *nm = in->as_->labels[li].name;
                while (*nm) strtab_buf[strtab_len++] = (uint8_t)(*nm++);
                strtab_buf[strtab_len++] = 0;
                uint64_t sym_vma = vma_text + (uint64_t)stub_len + (uint64_t)in->as_->labels[li].offset;
                if (is64) {
                    e_pu32(symtab_buf, &soff, (uint32_t)name_off);
                    e_pu8 (symtab_buf, &soff, 0x12); /* STB_GLOBAL<<4 | STT_FUNC */
                    e_pu8 (symtab_buf, &soff, 0x00);
                    e_pu16(symtab_buf, &soff, 5);    /* shndx: .text is section index 5 (see shnames[] below) */
                    e_pu64(symtab_buf, &soff, sym_vma);
                    e_pu64(symtab_buf, &soff, 0);    /* size unknown */
                } else {
                    e_pu32(symtab_buf, &soff, (uint32_t)name_off);
                    e_pu32(symtab_buf, &soff, (uint32_t)sym_vma);
                    e_pu32(symtab_buf, &soff, 0);
                    e_pu8 (symtab_buf, &soff, 0x12);
                    e_pu8 (symtab_buf, &soff, 0x00);
                    e_pu16(symtab_buf, &soff, 5);
                }
            }
        }
    }

    /* .shstrtab: names of the section headers themselves.
     * Deliberately NOT a "static const char *shnames[] = {...}" array of
     * string-literal pointers -- squash's own codegen cannot initialize a
     * pointer-typed array with a brace initializer (a real, general, and
     * already-documented limitation elsewhere in this same file, see
     * elf_grp_add's own "Avoid global pointer arrays" comment above): the
     * per-element initializers aren't compile-time-constant INTEGER
     * expressions (const_expr_eval() only ever handles those), so the
     * whole initializer silently falls back to a zero-filled block with
     * no runtime init code emitted at all -- every element stays NULL.
     * Confirmed via a real self-hosted (gen0-built) squash binary: it
     * segfaulted calling strlen() on a NULL section name pulled from
     * exactly this pattern, on literally "int main(){return 0;}" (a
     * self-hosting regression introduced by an earlier version of this
     * very section-header addition). An explicit index->string function
     * has no array-of-pointers to initialize at all, sidestepping the
     * bug entirely. */
    #define N_SHDRS 13
    int shname_off[N_SHDRS];
    int shstrtab_cap = 1;
    { int si; for (si = 0; si < N_SHDRS; si++) shstrtab_cap += (int)strlen(elf_shdr_name(si)) + 1; }
    uint8_t *shstrtab_buf = (uint8_t *)calloc((size_t)shstrtab_cap + 1, 1);
    int shstrtab_len = 0;
    shstrtab_buf[shstrtab_len++] = 0; /* index 0 = empty string, used by the NULL section */
    {
        int si;
        for (si = 0; si < N_SHDRS; si++) {
            shname_off[si] = shstrtab_len;
            const char *nm = elf_shdr_name(si);
            while (*nm) shstrtab_buf[shstrtab_len++] = (uint8_t)(*nm++);
            shstrtab_buf[shstrtab_len++] = 0;
        }
    }
    shname_off[0] = 0; /* NULL section's name is always index 0 (empty string) */

    int shdr_size = is64 ? 64 : 40;

    /* These three sections live past the end of the program image (outside
     * every PT_LOAD segment) -- pure file-only metadata, never mapped. */
    int off_symtab   = elf_align_up(rw_seg_filesz, 8);
    int off_strtab   = off_symtab + symtab_sz;
    int off_shstrtab = off_strtab + strtab_len;
    int off_shdrs    = elf_align_up(off_shstrtab + shstrtab_len, 8);
    int shdrs_end    = off_shdrs + N_SHDRS * shdr_size;

    /* ---- Step 10: ELF header + program headers ---- */
    int hdr_buf_sz = ehdr_size + n_phdrs * phdr_size;
    uint8_t *hdr_buf = (uint8_t *)calloc(hdr_buf_sz + 1, 1);
    {
        int hoff = 0;
        /* ELF ident */
        e_pu8(hdr_buf,&hoff,0x7F); e_pu8(hdr_buf,&hoff,'E');
        e_pu8(hdr_buf,&hoff,'L');  e_pu8(hdr_buf,&hoff,'F');
        e_pu8(hdr_buf,&hoff, is64 ? 2 : 1); /* EI_CLASS */
        e_pu8(hdr_buf,&hoff,1); /* EI_DATA: LE */
        e_pu8(hdr_buf,&hoff,1); /* EI_VERSION */
        e_pu8(hdr_buf,&hoff, in->is_openbsd ? 12 : 0); /* EI_OSABI: ELFOSABI_OPENBSD or ELFOSABI_NONE */
        { int pi=0; while(pi<8){e_pu8(hdr_buf,&hoff,0);pi++;} }
        if (is64) {
            e_pu16(hdr_buf,&hoff,2);    /* ET_EXEC */
            e_pu16(hdr_buf,&hoff, in->is_arm64 ? 183 : 0x3E); /* EM_AARCH64 or EM_X86_64 */
            e_pu32(hdr_buf,&hoff,1);
            e_pu64(hdr_buf,&hoff,entry_vma);
            e_pu64(hdr_buf,&hoff,(uint64_t)off_phdrs);
            e_pu64(hdr_buf,&hoff, in->strip_debug_sections ? 0 : (uint64_t)off_shdrs);
            e_pu32(hdr_buf,&hoff,0);
            e_pu16(hdr_buf,&hoff,(uint16_t)ehdr_size);
            e_pu16(hdr_buf,&hoff,(uint16_t)phdr_size);
            e_pu16(hdr_buf,&hoff,(uint16_t)n_phdrs);
            e_pu16(hdr_buf,&hoff, in->strip_debug_sections ? 0 : (uint16_t)shdr_size);
            e_pu16(hdr_buf,&hoff, in->strip_debug_sections ? 0 : (uint16_t)N_SHDRS);
            e_pu16(hdr_buf,&hoff, in->strip_debug_sections ? 0 : 12); /* e_shstrndx: .shstrtab is section index 12 */
        } else {
            e_pu16(hdr_buf,&hoff,2); e_pu16(hdr_buf,&hoff,3); /* EM_386 */
            e_pu32(hdr_buf,&hoff,1);
            e_pu32(hdr_buf,&hoff,(uint32_t)entry_vma);
            e_pu32(hdr_buf,&hoff,(uint32_t)off_phdrs);
            e_pu32(hdr_buf,&hoff, in->strip_debug_sections ? 0 : (uint32_t)off_shdrs); e_pu32(hdr_buf,&hoff,0);
            e_pu16(hdr_buf,&hoff,(uint16_t)ehdr_size);
            e_pu16(hdr_buf,&hoff,(uint16_t)phdr_size);
            e_pu16(hdr_buf,&hoff,(uint16_t)n_phdrs);
            e_pu16(hdr_buf,&hoff, in->strip_debug_sections ? 0 : (uint16_t)shdr_size);
            e_pu16(hdr_buf,&hoff, in->strip_debug_sections ? 0 : (uint16_t)N_SHDRS);
            e_pu16(hdr_buf,&hoff, in->strip_debug_sections ? 0 : 12);
        }

        /* Program headers */
        if (is64) {
            /* 64-bit Phdr layout: type(4) flags(4) offset(8) vaddr(8) paddr(8) filesz(8) memsz(8) align(8) */
            /* PT_PHDR */
            e_pu32(hdr_buf,&hoff,6); e_pu32(hdr_buf,&hoff,4);
            e_pu64(hdr_buf,&hoff,(uint64_t)off_phdrs);
            e_pu64(hdr_buf,&hoff,vma_rx+(uint64_t)off_phdrs);   /* vaddr */
            e_pu64(hdr_buf,&hoff,vma_rx+(uint64_t)off_phdrs);   /* paddr */
            e_pu64(hdr_buf,&hoff,(uint64_t)(n_phdrs*phdr_size));
            e_pu64(hdr_buf,&hoff,(uint64_t)(n_phdrs*phdr_size));
            e_pu64(hdr_buf,&hoff,8);
            /* PT_NOTE (OpenBSD branding only — see note_buf comment above) */
            if (in->is_openbsd) {
                e_pu32(hdr_buf,&hoff,4); e_pu32(hdr_buf,&hoff,4);
                e_pu64(hdr_buf,&hoff,(uint64_t)off_note);
                e_pu64(hdr_buf,&hoff,vma_note);   /* vaddr */
                e_pu64(hdr_buf,&hoff,vma_note);   /* paddr */
                e_pu64(hdr_buf,&hoff,(uint64_t)note_len);
                e_pu64(hdr_buf,&hoff,(uint64_t)note_len);
                e_pu64(hdr_buf,&hoff,4);
            }
            /* PT_INTERP */
            e_pu32(hdr_buf,&hoff,3); e_pu32(hdr_buf,&hoff,4);
            e_pu64(hdr_buf,&hoff,(uint64_t)off_interp);
            e_pu64(hdr_buf,&hoff,vma_interp);   /* vaddr */
            e_pu64(hdr_buf,&hoff,vma_interp);   /* paddr */
            e_pu64(hdr_buf,&hoff,(uint64_t)interp_len);
            e_pu64(hdr_buf,&hoff,(uint64_t)interp_len);
            e_pu64(hdr_buf,&hoff,1);
            /* PT_LOAD rx */
            e_pu32(hdr_buf,&hoff,1); e_pu32(hdr_buf,&hoff,5);
            e_pu64(hdr_buf,&hoff,0);
            e_pu64(hdr_buf,&hoff,vma_rx);   /* vaddr */
            e_pu64(hdr_buf,&hoff,vma_rx);   /* paddr */
            e_pu64(hdr_buf,&hoff,(uint64_t)rx_seg_filesz);
            e_pu64(hdr_buf,&hoff,(uint64_t)rx_seg_filesz);
            e_pu64(hdr_buf,&hoff,(uint64_t)page);
            /* PT_LOAD rw */
            e_pu32(hdr_buf,&hoff,1); e_pu32(hdr_buf,&hoff,6);
            e_pu64(hdr_buf,&hoff,(uint64_t)off_got);
            e_pu64(hdr_buf,&hoff,vma_rw);   /* vaddr */
            e_pu64(hdr_buf,&hoff,vma_rw);   /* paddr */
            e_pu64(hdr_buf,&hoff,(uint64_t)(rw_seg_filesz-off_got));
            e_pu64(hdr_buf,&hoff,(uint64_t)(rw_seg_filesz-off_got));
            e_pu64(hdr_buf,&hoff,(uint64_t)page);
            /* PT_DYNAMIC */
            e_pu32(hdr_buf,&hoff,2); e_pu32(hdr_buf,&hoff,6);
            e_pu64(hdr_buf,&hoff,(uint64_t)off_dynamic);
            e_pu64(hdr_buf,&hoff,vma_dynamic);   /* vaddr */
            e_pu64(hdr_buf,&hoff,vma_dynamic);   /* paddr */
            e_pu64(hdr_buf,&hoff,(uint64_t)dynamic_sz);
            e_pu64(hdr_buf,&hoff,(uint64_t)dynamic_sz);
            e_pu64(hdr_buf,&hoff,8);
            /* PT_GNU_STACK */
            e_pu32(hdr_buf,&hoff,0x6474e551); e_pu32(hdr_buf,&hoff,6);
            e_pu64(hdr_buf,&hoff,0);   /* offset */
            e_pu64(hdr_buf,&hoff,0);   /* vaddr */
            e_pu64(hdr_buf,&hoff,0);   /* paddr */
            e_pu64(hdr_buf,&hoff,0);   /* filesz */
            e_pu64(hdr_buf,&hoff,0);   /* memsz */
            e_pu64(hdr_buf,&hoff,0x10); /* align */
        } else {
            /* PT_PHDR */
            e_pu32(hdr_buf,&hoff,6);
            e_pu32(hdr_buf,&hoff,(uint32_t)off_phdrs);
            e_pu32(hdr_buf,&hoff,(uint32_t)(vma_rx+(uint64_t)off_phdrs));
            e_pu32(hdr_buf,&hoff,(uint32_t)(vma_rx+(uint64_t)off_phdrs));
            e_pu32(hdr_buf,&hoff,(uint32_t)(n_phdrs*phdr_size));
            e_pu32(hdr_buf,&hoff,(uint32_t)(n_phdrs*phdr_size));
            e_pu32(hdr_buf,&hoff,4); e_pu32(hdr_buf,&hoff,4);
            /* PT_NOTE (OpenBSD branding only — see note_buf comment above) */
            if (in->is_openbsd) {
                e_pu32(hdr_buf,&hoff,4);
                e_pu32(hdr_buf,&hoff,(uint32_t)off_note);
                e_pu32(hdr_buf,&hoff,(uint32_t)vma_note);
                e_pu32(hdr_buf,&hoff,(uint32_t)vma_note);
                e_pu32(hdr_buf,&hoff,(uint32_t)note_len);
                e_pu32(hdr_buf,&hoff,(uint32_t)note_len);
                e_pu32(hdr_buf,&hoff,4); e_pu32(hdr_buf,&hoff,4);
            }
            /* PT_INTERP */
            e_pu32(hdr_buf,&hoff,3);
            e_pu32(hdr_buf,&hoff,(uint32_t)off_interp);
            e_pu32(hdr_buf,&hoff,(uint32_t)vma_interp);
            e_pu32(hdr_buf,&hoff,(uint32_t)vma_interp);
            e_pu32(hdr_buf,&hoff,(uint32_t)interp_len);
            e_pu32(hdr_buf,&hoff,(uint32_t)interp_len);
            e_pu32(hdr_buf,&hoff,4); e_pu32(hdr_buf,&hoff,1);
            /* PT_LOAD rx */
            e_pu32(hdr_buf,&hoff,1);
            e_pu32(hdr_buf,&hoff,0);
            e_pu32(hdr_buf,&hoff,(uint32_t)vma_rx);
            e_pu32(hdr_buf,&hoff,(uint32_t)vma_rx);
            e_pu32(hdr_buf,&hoff,(uint32_t)rx_seg_filesz);
            e_pu32(hdr_buf,&hoff,(uint32_t)rx_seg_filesz);
            e_pu32(hdr_buf,&hoff,5); e_pu32(hdr_buf,&hoff,(uint32_t)page);
            /* PT_LOAD rw */
            e_pu32(hdr_buf,&hoff,1);
            e_pu32(hdr_buf,&hoff,(uint32_t)off_got);
            e_pu32(hdr_buf,&hoff,(uint32_t)vma_rw);
            e_pu32(hdr_buf,&hoff,(uint32_t)vma_rw);
            e_pu32(hdr_buf,&hoff,(uint32_t)(rw_seg_filesz-off_got));
            e_pu32(hdr_buf,&hoff,(uint32_t)(rw_seg_filesz-off_got));
            e_pu32(hdr_buf,&hoff,6); e_pu32(hdr_buf,&hoff,(uint32_t)page);
            /* PT_DYNAMIC */
            e_pu32(hdr_buf,&hoff,2);
            e_pu32(hdr_buf,&hoff,(uint32_t)off_dynamic);
            e_pu32(hdr_buf,&hoff,(uint32_t)vma_dynamic);
            e_pu32(hdr_buf,&hoff,(uint32_t)vma_dynamic);
            e_pu32(hdr_buf,&hoff,(uint32_t)dynamic_sz);
            e_pu32(hdr_buf,&hoff,(uint32_t)dynamic_sz);
            e_pu32(hdr_buf,&hoff,6); e_pu32(hdr_buf,&hoff,8);
            /* PT_GNU_STACK */
            e_pu32(hdr_buf,&hoff,0x6474e551);
            e_pu32(hdr_buf,&hoff,0); e_pu32(hdr_buf,&hoff,0); e_pu32(hdr_buf,&hoff,0);
            e_pu32(hdr_buf,&hoff,0); e_pu32(hdr_buf,&hoff,0);
            e_pu32(hdr_buf,&hoff,6); e_pu32(hdr_buf,&hoff,0x10);
        }
    }

    /* ---- Step 10.5: section header table bytes ----
     * One Shdr per shnames[] entry, in the exact same order (their array
     * index IS their section index, referenced above by the .symtab
     * entries' shndx=5 and by e_shstrndx=12). */
    uint8_t *shdr_buf = (uint8_t *)calloc((size_t)(N_SHDRS * shdr_size) + 1, 1);
    {
        int so = 0;
        #define SH_NULL 0
        #define SH_PROGBITS 1
        #define SH_SYMTAB 2
        #define SH_STRTAB 3
        #define SH_RELA 4
        #define SH_DYNAMIC 6
        #define SH_DYNSYM 11
        #define SHF_WRITE 1
        #define SHF_ALLOC 2
        #define SHF_EXECINSTR 4
        /* WRITE_SHDR(name_idx, type, flags, addr, offset, size, link, info, align, entsize) */
        #define WRITE_SHDR(NAMEI,TYPE,FLAGS,ADDR,OFFSET,SIZE,LINK,INFO,ALIGN,ENTSIZE) do { \
            if (is64) { \
                e_pu32(shdr_buf,&so,(uint32_t)(NAMEI)); e_pu32(shdr_buf,&so,(uint32_t)(TYPE)); \
                e_pu64(shdr_buf,&so,(uint64_t)(FLAGS)); e_pu64(shdr_buf,&so,(uint64_t)(ADDR)); \
                e_pu64(shdr_buf,&so,(uint64_t)(OFFSET)); e_pu64(shdr_buf,&so,(uint64_t)(SIZE)); \
                e_pu32(shdr_buf,&so,(uint32_t)(LINK)); e_pu32(shdr_buf,&so,(uint32_t)(INFO)); \
                e_pu64(shdr_buf,&so,(uint64_t)(ALIGN)); e_pu64(shdr_buf,&so,(uint64_t)(ENTSIZE)); \
            } else { \
                e_pu32(shdr_buf,&so,(uint32_t)(NAMEI)); e_pu32(shdr_buf,&so,(uint32_t)(TYPE)); \
                e_pu32(shdr_buf,&so,(uint32_t)(FLAGS)); e_pu32(shdr_buf,&so,(uint32_t)(ADDR)); \
                e_pu32(shdr_buf,&so,(uint32_t)(OFFSET)); e_pu32(shdr_buf,&so,(uint32_t)(SIZE)); \
                e_pu32(shdr_buf,&so,(uint32_t)(LINK)); e_pu32(shdr_buf,&so,(uint32_t)(INFO)); \
                e_pu32(shdr_buf,&so,(uint32_t)(ALIGN)); e_pu32(shdr_buf,&so,(uint32_t)(ENTSIZE)); \
            } \
        } while (0)

        WRITE_SHDR(shname_off[0], SH_NULL, 0, 0, 0, 0, 0, 0, 0, 0); /* index 0: mandatory NULL */
        WRITE_SHDR(shname_off[1], SH_PROGBITS, SHF_ALLOC, vma_interp, off_interp, interp_len, 0, 0, 1, 0); /* .interp */
        WRITE_SHDR(shname_off[2], SH_STRTAB, SHF_ALLOC, vma_dynstr, off_dynstr, dynstr_sz, 0, 0, 1, 0); /* .dynstr */
        WRITE_SHDR(shname_off[3], SH_DYNSYM, SHF_ALLOC, vma_dynsym, off_dynsym, dynsym_sz, 2, 1, 8, sym_entry_size); /* .dynsym */
        WRITE_SHDR(shname_off[4], SH_RELA, SHF_ALLOC, vma_reloc, off_reloc, relasz, 3, 0, 8, rela_entry_size); /* .rela.dyn */
        WRITE_SHDR(shname_off[5], SH_PROGBITS, SHF_ALLOC|SHF_EXECINSTR, vma_text, off_text, text_len, 0, 0, 16, 0); /* .text */
        WRITE_SHDR(shname_off[6], SH_PROGBITS, SHF_ALLOC, vma_rodata, off_rodata, rodata_sz, 0, 0, 1, 0); /* .rodata */
        WRITE_SHDR(shname_off[7], SH_PROGBITS, SHF_ALLOC|SHF_WRITE, vma_got, off_got, got_sz, 0, 0, got_slot_size, got_slot_size); /* .got */
        WRITE_SHDR(shname_off[8], SH_DYNAMIC, SHF_ALLOC|SHF_WRITE, vma_dynamic, off_dynamic, dynamic_sz, 2, 0, 8, dyn_entry_size); /* .dynamic */
        WRITE_SHDR(shname_off[9], SH_PROGBITS, SHF_ALLOC|SHF_WRITE, vma_data, off_data, data_sz, 0, 0, 1, 0); /* .data */
        WRITE_SHDR(shname_off[10], SH_SYMTAB, 0, 0, off_symtab, symtab_sz, 11, 1, 8, sym_entry_size); /* .symtab */
        WRITE_SHDR(shname_off[11], SH_STRTAB, 0, 0, off_strtab, strtab_len, 0, 0, 1, 0); /* .strtab */
        WRITE_SHDR(shname_off[12], SH_STRTAB, 0, 0, off_shstrtab, shstrtab_len, 0, 0, 1, 0); /* .shstrtab */
        #undef WRITE_SHDR
    }

    /* ---- Step 11: write file ---- */
    FILE *fp = fopen(in->output_path, "wb");
    if (!fp) {
        diag_emit(DIAG_ERROR, -1, NULL, NULL, "cannot open output %s", in->output_path);
        free(dynstr); free(dynsym_buf); free(reloc_buf);
        free(dynamic_buf); free(hdr_buf); free(text);
        return 1;
    }

    fwrite(hdr_buf, 1, hdr_buf_sz, fp);
    if (note_len > 0) fwrite(note_buf, 1, note_len, fp);
    fwrite(interp_str, 1, interp_len, fp);
    fwrite(dynstr, 1, dynstr_sz, fp);

    /* pad to dynsym */
    { int cur = off_dynstr + dynstr_sz; int need = off_dynsym - cur;
      int _n = need; while(_n-->0) { uint8_t _z=0; fwrite(&_z,1,1,fp); } }

    fwrite(dynsym_buf, 1, dynsym_sz, fp);

    /* pad to reloc */
    { int cur = off_dynsym + dynsym_sz; int need = off_reloc - cur;
      int _n = need; while(_n-->0) { uint8_t _z=0; fwrite(&_z,1,1,fp); } }

    fwrite(reloc_buf, 1, relasz, fp);

    /* pad to text */
    { int cur = off_reloc + relasz; int need = off_text - cur;
      int _n = need; while(_n-->0) { uint8_t _z=0; fwrite(&_z,1,1,fp); } }

    fwrite(text, 1, text_len, fp);

    if (in->rdata_strings && in->rdata_strings_len > 0)
        fwrite(in->rdata_strings, 1, in->rdata_strings_len, fp);
    if (static_rdata_bytes && static_rdata_len > 0)
        fwrite(static_rdata_bytes, 1, static_rdata_len, fp);

    /* pad to next page */
    { int cur = off_rodata + rodata_sz; int need = rx_seg_filesz_padded - cur;
      int _n = need; while(_n-->0) { uint8_t _z=0; fwrite(&_z,1,1,fp); } }

    /* .got (zero-initialized) */
    { int _n = got_sz; while(_n-->0) { uint8_t _z=0; fwrite(&_z,1,1,fp); } }

    /* pad to dynamic */
    { int cur = off_got + got_sz; int need = off_dynamic - cur;
      int _n = need; while(_n-->0) { uint8_t _z=0; fwrite(&_z,1,1,fp); } }

    fwrite(dynamic_buf, 1, dynamic_sz, fp);

    /* pad to data */
    { int cur = off_dynamic + dynamic_sz; int need = off_data - cur;
      int _n = need; while(_n-->0) { uint8_t _z=0; fwrite(&_z,1,1,fp); } }

    if (in->wdata_bytes && in->wdata_len > 0)
        fwrite(in->wdata_bytes, 1, in->wdata_len, fp);

    if (!in->strip_debug_sections) {
        /* pad to symtab (this is past the end of the last real PT_LOAD
         * segment -- everything from here on is file-only metadata, never
         * mapped at runtime, so program behavior is unaffected by anything
         * below). */
        { int cur = off_data + data_sz; int need = off_symtab - cur;
          int _n = need; while(_n-->0) { uint8_t _z=0; fwrite(&_z,1,1,fp); } }

        fwrite(symtab_buf, 1, (size_t)symtab_sz, fp);
        fwrite(strtab_buf, 1, (size_t)strtab_len, fp);
        fwrite(shstrtab_buf, 1, (size_t)shstrtab_len, fp);

        /* pad to the section header table itself */
        { int cur = off_shstrtab + shstrtab_len; int need = off_shdrs - cur;
          int _n = need; while(_n-->0) { uint8_t _z=0; fwrite(&_z,1,1,fp); } }

        fwrite(shdr_buf, 1, (size_t)(N_SHDRS * shdr_size), fp);
    }

    fclose(fp);
    chmod(in->output_path, 0x1ED); /* 0755 octal = rwxr-xr-x */

    printf("  %-10s file=0x%04X  addr=0x%04X  size=%u\n",
           ".interp",   off_interp,  (unsigned)vma_interp,  (unsigned)interp_len);
    printf("  %-10s file=0x%04X  addr=0x%04X  size=%u\n",
           ".dynstr",   off_dynstr,  (unsigned)vma_dynstr,  (unsigned)dynstr_sz);
    printf("  %-10s file=0x%04X  addr=0x%04X  size=%u\n",
           ".dynsym",   off_dynsym,  (unsigned)vma_dynsym,  (unsigned)dynsym_sz);
    printf("  %-10s file=0x%04X  addr=0x%04X  size=%u\n",
           ".rela.dyn", off_reloc,   (unsigned)vma_reloc,   (unsigned)relasz);
    printf("  %-10s file=0x%04X  addr=0x%04X  size=%u\n",
           ".text",     off_text,    (unsigned)vma_text,    (unsigned)text_len);
    printf("  %-10s file=0x%04X  addr=0x%04X  size=%u\n",
           ".rodata",   off_rodata,  (unsigned)vma_rodata,  (unsigned)rodata_sz);
    printf("  %-10s file=0x%04X  addr=0x%04X  size=%u\n",
           ".got",      off_got,     (unsigned)vma_got,     (unsigned)got_sz);
    printf("  %-10s file=0x%04X  addr=0x%04X  size=%u\n",
           ".dynamic",  off_dynamic, (unsigned)vma_dynamic, (unsigned)dynamic_sz);
    printf("  %-10s file=0x%04X  addr=0x%04X  size=%u\n",
           ".data",     off_data,    (unsigned)vma_data,    (unsigned)data_sz);
    if (!in->strip_debug_sections) {
        printf("  %-10s file=0x%04X  size=%u\n", ".symtab",   off_symtab,   (unsigned)symtab_sz);
        printf("  %-10s file=0x%04X  size=%u\n", ".strtab",   off_strtab,   (unsigned)strtab_len);
        printf("  %-10s file=0x%04X  size=%u\n", ".shstrtab", off_shstrtab, (unsigned)shstrtab_len);
    }

    printf("ELF written: %s (%d bytes)\n", in->output_path,
           in->strip_debug_sections ? rw_seg_filesz : shdrs_end);

    free(dynstr); free(dynsym_buf); free(reloc_buf);
    free(dynamic_buf); free(hdr_buf); free(text);
    free(symtab_buf); free(strtab_buf); free(shstrtab_buf); free(shdr_buf);
    return 0;
}
