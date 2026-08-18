#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include "macho_builder.h"
#include "diag.h"

/* =========================================================================
 * macho_builder — Mach-O (macOS) x86-64 executable writer
 *
 * Sibling of elf_builder.c. The two share their whole input contract and
 * their relocation semantics; only the container differs. Read
 * elf_builder.c first if you're new to this layer — the section layout,
 * the GOT-slot-per-import scheme and the relocation switch below are all
 * direct ports of what that file does, and the comments here concentrate
 * on the places where Mach-O genuinely forced a different decision.
 *
 * The three that matter:
 *
 *  1. A much smaller entry stub, and it lives at the END of __text.
 *     LC_MAIN removes most of the ELF stub's job: dyld hands the entry
 *     offset to libSystem's start(), which already calls it as
 *     main(argc, argv, envp, apple) on a correctly aligned stack, so there
 *     is no argc/argv-off-the-raw-stack setup to do.
 *
 *     What does NOT go away is the call to exit(). squash's codegen
 *     clobbers callee-saved registers (rbx in particular -- it uses it as
 *     a general scratch register throughout expression evaluation). On
 *     Linux that never shows, because the ELF _start stub calls exit()
 *     the instant main returns and nothing ever reads those registers
 *     again. Under LC_MAIN, main returns into libSystem's start(), which
 *     very much does still use rbx -- so returning normally from main
 *     crashes the process AFTER it has produced all its correct output,
 *     inside dyld, with a backtrace pointing nowhere near the real cause.
 *     Routing main's return value straight into exit() sidesteps it the
 *     same way the ELF path already does (and gets the stdio flush too).
 *
 *     The stub is appended after the user text rather than prepended
 *     because, unlike ELF, nothing here needs it at a fixed address --
 *     LC_MAIN names the entry by offset. Appending keeps every relocation
 *     offset equal to its offset in the codegen text buffer, so none of
 *     the "+ stub_len" adjustments that thread through elf_builder.c are
 *     needed.
 *
 *  2. Non-PIE, image base 0x1_0000_0000. Every address in the file is
 *     therefore (0x100000000 + its own file offset), which keeps file
 *     offsets and virtual addresses in lockstep exactly like the ELF path.
 *     Because the base is fixed at link time there is nothing to rebase,
 *     so LC_DYLD_INFO_ONLY carries a bind stream and nothing else.
 *     Addresses no longer fit in 32 bits though, so unlike elf_builder.c's
 *     e_pu64 (which hardcodes a zero high word) every 64-bit write here
 *     goes through m_pu64_hl and passes the high word explicitly -- both
 *     to be correct and to stay compilable by squash itself, whose 32-bit
 *     mode can't shift a 64-bit value.
 *
 *  3. Imports bind at load into __DATA,__got, a S_NON_LAZY_SYMBOL_POINTERS
 *     section. That is the direct analogue of the ELF path's GOT +
 *     DF_BIND_NOW choice, and it means RELOC_IAT_REL32 lowers to the exact
 *     same "call qword ptr [rip+disp32]" on both platforms -- which is why
 *     codegen.c needs no macOS branch.
 * ========================================================================= */

/* ---- little-endian writers (m_ prefix: pe_/elf_ builders own e_/p_) ---- */
static void m_pu8 (uint8_t *b, int *off, uint8_t  v) { b[(*off)++] = v; }
static void m_pu16(uint8_t *b, int *off, uint16_t v) {
    b[(*off)++]=(uint8_t)(v); b[(*off)++]=(uint8_t)(v>>8);
}
static void m_pu32(uint8_t *b, int *off, uint32_t v) {
    b[(*off)++]=(uint8_t)(v);      b[(*off)++]=(uint8_t)(v>>8);
    b[(*off)++]=(uint8_t)(v>>16);  b[(*off)++]=(uint8_t)(v>>24);
}
/* 64-bit write as explicit high:low 32-bit halves. Mach-O executable
 * addresses live above 4GB (see the image-base note above), so unlike
 * elf_builder.c's e_pu64 the high word is genuinely used -- and splitting
 * it this way avoids any 64-bit shift, keeping this file compilable by
 * squash's own 32-bit mode. */
static void m_pu64_hl(uint8_t *b, int *off, uint32_t hi, uint32_t lo) {
    m_pu32(b, off, lo); m_pu32(b, off, hi);
}
static void m_patch32(uint8_t *b, int off, uint32_t v) {
    b[off+0]=(uint8_t)(v);     b[off+1]=(uint8_t)(v>>8);
    b[off+2]=(uint8_t)(v>>16); b[off+3]=(uint8_t)(v>>24);
}
/* Fixed-size, NUL-padded segname/sectname field (Mach-O uses char[16],
 * NOT NUL-terminated when the name is exactly 16 chars long). */
static void m_pname16(uint8_t *b, int *off, const char *s) {
    int n = (int)strlen(s); if (n > 16) n = 16;
    int i = 0;
    while (i < n)  { b[(*off)++] = (uint8_t)s[i]; i++; }
    while (i < 16) { b[(*off)++] = 0; i++; }
}
static int m_align_up(int v, int a) { return (v + a - 1) & ~(a - 1); }

/* ULEB128 — the encoding dyld's bind opcode stream uses for offsets. */
static void m_uleb(uint8_t *b, int *off, uint32_t v) {
    while (v >= 0x80) { b[(*off)++] = (uint8_t)((v & 0x7F) | 0x80); v >>= 7; }
    b[(*off)++] = (uint8_t)v;
}

/* ---- Mach-O constants (from <mach-o/loader.h>) ---- */
#define MH_MAGIC_64      0xFEEDFACFu
#define CPU_TYPE_X86_64  0x01000007u
#define CPU_SUBTYPE_X86_64_ALL 3u
#define MH_EXECUTE       2u
#define MH_NOUNDEFS      0x1u
#define MH_DYLDLINK      0x4u
#define MH_TWOLEVEL      0x80u

#define LC_REQ_DYLD      0x80000000u
#define LC_SEGMENT_64    0x19u
#define LC_SYMTAB        0x02u
#define LC_DYSYMTAB      0x0Bu
#define LC_LOAD_DYLIB    0x0Cu
#define LC_LOAD_DYLINKER 0x0Eu
#define LC_UUID          0x1Bu
#define LC_SOURCE_VERSION 0x2Au
#define LC_BUILD_VERSION 0x32u
#define LC_DYLD_INFO_ONLY (0x22u | LC_REQ_DYLD)
#define LC_MAIN          (0x28u | LC_REQ_DYLD)

#define S_NON_LAZY_SYMBOL_POINTERS 0x6u

#define VM_PROT_READ  1u
#define VM_PROT_WRITE 2u
#define VM_PROT_EXEC  4u

/* Bind opcodes (high nibble = opcode, low nibble = immediate) */
#define BIND_OPCODE_DONE                     0x00u
#define BIND_OPCODE_SET_DYLIB_ORDINAL_IMM    0x10u
#define BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM 0x40u
#define BIND_OPCODE_SET_TYPE_IMM             0x50u
#define BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB 0x70u
#define BIND_OPCODE_DO_BIND                  0x90u
#define BIND_TYPE_POINTER                    1u

/* Segment index of __DATA in the load-command order this file emits:
 * __PAGEZERO(0), __TEXT(1), __DATA(2), __LINKEDIT(3). The bind stream
 * addresses __got by this index, so the two must be kept in sync. */
#define MACHO_DATA_SEG_INDEX 2

/* Image base for a non-PIE 64-bit executable. __PAGEZERO covers exactly
 * this much address space so that a null-pointer dereference still faults. */
#define MACHO_IMAGE_BASE_HI 1u
#define MACHO_IMAGE_BASE_LO 0u
#define MACHO_PAGE 4096

/* =========================================================================
 * Import grouping by dylib
 *
 * Same flat-static-array shape (and the same reasons for it) as
 * elf_builder.c's elf_grp_* -- see the long comment there. The one real
 * difference: grouping happens by the *resolved macOS dylib path*, not by
 * the library name codegen emitted. codegen.c tags every Unix libc import
 * with a glibc soname ("libc.so.6:printf", "libm.so.6:sin", ...) because
 * that is what the ELF backend needs; on macOS all of those symbols live
 * in a single library, libSystem. Mapping here rather than in codegen
 * keeps the ~40 hardcoded "libc.so.6:%s" sites in codegen.c untouched and
 * collapses libc/libm/libpthread/libdl/librt onto one LC_LOAD_DYLIB.
 * ========================================================================= */
#define MACHO_MAX_LIBS  8
#define MACHO_MAX_FUNCS 256
#define MACHO_FUNC_NLEN 80
#define MACHO_LIB_NLEN  128

static char macho_lib_names[MACHO_MAX_LIBS * MACHO_LIB_NLEN]; /* dylib i path at i*128 */
static int  macho_nlibs;

static char macho_func_names[MACHO_MAX_FUNCS * MACHO_FUNC_NLEN]; /* func i name at i*80 */
static int  macho_func_lib[MACHO_MAX_FUNCS];   /* dylib index for func i */
static int  macho_total_funcs;

static void macho_grp_reset(void) { macho_nlibs = 0; macho_total_funcs = 0; }

/* Map a codegen-emitted Unix library name onto the macOS dylib that really
 * exports the symbol. Everything glibc splits across several sonames is a
 * single library on macOS, so the common case folds to libSystem. */
static const char *macho_dylib_path(const char *lib) {
    if (strcmp(lib,"libc.so.6")==0      || strcmp(lib,"libc")==0      ||
        strcmp(lib,"libm.so.6")==0      || strcmp(lib,"libm")==0      ||
        strcmp(lib,"libpthread.so.0")==0|| strcmp(lib,"libpthread")==0||
        strcmp(lib,"libdl.so.2")==0     || strcmp(lib,"libdl")==0     ||
        strcmp(lib,"librt.so.1")==0     || strcmp(lib,"librt")==0)
        return "/usr/lib/libSystem.B.dylib";
    return NULL; /* caller falls back to a derived /usr/lib/libNAME.dylib */
}

static void macho_grp_add(const char *spec) {
    char lib[MACHO_LIB_NLEN];
    char func[MACHO_FUNC_NLEN];
    char path[MACHO_LIB_NLEN];
    const char *colon = spec;
    while (*colon && *colon != ':') colon++;
    if (!*colon) return;
    int liblen = (int)(colon - spec);
    if (liblen >= MACHO_LIB_NLEN-1) liblen = MACHO_LIB_NLEN-2;
    int ii = 0;
    while (ii < liblen) { lib[ii] = spec[ii]; ii++; }
    lib[ii] = '\0';

    /* Skip the non-dynamic markers, exactly as the ELF path does: "extern:"
     * means "resolved statically elsewhere", and a ".dll" name is a Windows
     * import that has no business in a macOS build. */
    if (strcmp(lib, "extern") == 0) return;
    { int ll = (int)strlen(lib);
      if (ll >= 4 && lib[ll-4]=='.' && lib[ll-3]=='d' && lib[ll-2]=='l' && lib[ll-1]=='l') return; }

    const char *mapped = macho_dylib_path(lib);
    if (mapped) {
        int k = 0; while (mapped[k] && k < MACHO_LIB_NLEN-1) { path[k] = mapped[k]; k++; }
        path[k] = '\0';
    } else {
        /* Best effort for a user "-lfoo": "libfoo.so.N" -> /usr/lib/libfoo.dylib */
        char bare[MACHO_LIB_NLEN];
        int k = 0;
        while (lib[k] && k < MACHO_LIB_NLEN-1) {
            if (lib[k]=='.' && lib[k+1]=='s' && lib[k+2]=='o') break;
            bare[k] = lib[k]; k++;
        }
        bare[k] = '\0';
        snprintf(path, sizeof path, "/usr/lib/%s.dylib", bare);
    }

    const char *fn = colon + 1;
    ii = 0;
    while (fn[ii] && ii < MACHO_FUNC_NLEN-1) { func[ii] = fn[ii]; ii++; }
    func[ii] = '\0';

    int lg = -1;
    int i = 0;
    while (i < macho_nlibs) {
        if (strcmp(macho_lib_names + i * MACHO_LIB_NLEN, path) == 0) { lg = i; break; }
        i++;
    }
    if (lg < 0) {
        if (macho_nlibs >= MACHO_MAX_LIBS) return;
        lg = macho_nlibs++;
        ii = 0;
        while (path[ii]) { macho_lib_names[lg * MACHO_LIB_NLEN + ii] = path[ii]; ii++; }
        macho_lib_names[lg * MACHO_LIB_NLEN + ii] = '\0';
    }
    /* A symbol name is unique across the whole process on macOS (flat C
     * namespace), so unlike the ELF path there's no need to key the
     * duplicate check on the library too -- and doing so would be wrong,
     * since libc.so.6:foo and libm.so.6:foo both map to libSystem here and
     * must collapse to a single GOT slot. */
    int fi = 0;
    while (fi < macho_total_funcs) {
        if (strcmp(macho_func_names + fi * MACHO_FUNC_NLEN, func) == 0) return;
        fi++;
    }
    if (macho_total_funcs >= MACHO_MAX_FUNCS) return;
    int slot = macho_total_funcs;
    ii = 0;
    while (func[ii]) { macho_func_names[slot * MACHO_FUNC_NLEN + ii] = func[ii]; ii++; }
    macho_func_names[slot * MACHO_FUNC_NLEN + ii] = '\0';
    macho_func_lib[slot] = lg;
    macho_total_funcs++;
}

static int macho_grp_got_idx(const char *fname) {
    int fi = 0;
    while (fi < macho_total_funcs) {
        if (strcmp(macho_func_names + fi * MACHO_FUNC_NLEN, fname) == 0) return fi;
        fi++;
    }
    return -1;
}

/* =========================================================================
 * macho_link_and_write
 * ========================================================================= */
int macho_link_and_write(MachOBuildInput *in) {
    /* ---- Step 1: group imports by dylib ---- */
    macho_grp_reset();
    /* exit() is always imported so the entry stub can hand main's return
     * value to it — see the entry-stub note in the header comment. Added
     * first so it always gets a GOT slot even if the program itself
     * imports nothing. */
    macho_grp_add("libc.so.6:exit");
    {
        int ii = 0;
        while (ii < in->import_count) { macho_grp_add(in->import_specs[ii]); ii++; }
    }
    int nfuncs = macho_total_funcs;

    /* The always-present exit import above also guarantees at least one
     * LC_LOAD_DYLIB (libSystem), which dyld requires of a two-level-
     * namespace executable and which LC_MAIN needs regardless, since
     * libSystem's start() is what calls the entry point. */

    /* ---- Step 2: __LINKEDIT payloads ---- */

    /* 2a: string table. Index 0 is reserved (a zero n_strx means "no
     * name"), so it starts with a NUL. C symbols carry a leading
     * underscore on macOS -- "printf" in the import spec is "_printf" in
     * the binary. */
    int strtab_cap = 2 + nfuncs * (MACHO_FUNC_NLEN + 2);
    uint8_t *strtab = (uint8_t *)calloc(strtab_cap, 1);
    int strtab_len = 0;
    strtab[strtab_len++] = 0;
    int *sym_name_off = (int *)calloc((size_t)(nfuncs > 0 ? nfuncs : 1), sizeof(int));
    {
        int fi = 0;
        while (fi < nfuncs) {
            sym_name_off[fi] = strtab_len;
            strtab[strtab_len++] = '_';
            const char *nm = macho_func_names + fi * MACHO_FUNC_NLEN;
            int k = 0;
            while (nm[k]) { strtab[strtab_len++] = (uint8_t)nm[k]; k++; }
            strtab[strtab_len++] = 0;
            fi++;
        }
    }
    int strtab_padded = m_align_up(strtab_len, 8);

    /* 2b: symbol table — one N_UNDF|N_EXT nlist_64 per import, in GOT-slot
     * order so the indirect symbol table below is just the identity map. */
    int nsyms = nfuncs;
    int symtab_sz = nsyms * 16;
    uint8_t *symtab = (uint8_t *)calloc(symtab_sz + 1, 1);
    {
        int soff = 0;
        int fi = 0;
        while (fi < nfuncs) {
            m_pu32(symtab, &soff, (uint32_t)sym_name_off[fi]); /* n_strx */
            m_pu8 (symtab, &soff, 0x01);                       /* n_type: N_UNDF|N_EXT */
            m_pu8 (symtab, &soff, 0x00);                       /* n_sect: NO_SECT */
            /* n_desc holds the two-level-namespace library ordinal in its
             * high byte — this is what tells dyld which dylib to search. */
            m_pu16(symtab, &soff, (uint16_t)((macho_func_lib[fi] + 1) << 8));
            m_pu64_hl(symtab, &soff, 0, 0);                    /* n_value */
            fi++;
        }
    }

    /* 2c: indirect symbol table — one u32 per __got slot, giving the
     * symbol index that slot points at. */
    int indirect_sz = nfuncs * 4;
    uint8_t *indirect = (uint8_t *)calloc(indirect_sz + 1, 1);
    {
        int ioff = 0;
        int fi = 0;
        while (fi < nfuncs) { m_pu32(indirect, &ioff, (uint32_t)fi); fi++; }
    }

    /* 2d: bind opcode stream. Written once the __got offset within __DATA
     * is known (Step 3), so only its size is estimated here — generously,
     * since it is bounded by a fixed prologue plus ~4 bytes and a name per
     * symbol. */
    int bind_cap = 32 + nfuncs * (MACHO_FUNC_NLEN + 16);
    uint8_t *bind = (uint8_t *)calloc(bind_cap, 1);
    int bind_len = 0;

    /* ---- Step 3: file layout ----
     * Load commands must be sized before anything can be placed, because
     * they sit between the Mach header and __text. Sizes are fixed by the
     * command set, except LC_LOAD_DYLIB which carries an inline path. */
    int dylib_cmd_sz[MACHO_MAX_LIBS];
    int lc_size = 0;
    int ncmds = 0;
    lc_size += 72;  ncmds++;   /* LC_SEGMENT_64 __PAGEZERO (no sections) */
    lc_size += 72 + 2*80; ncmds++; /* LC_SEGMENT_64 __TEXT + __text,__const */
    lc_size += 72 + 2*80; ncmds++; /* LC_SEGMENT_64 __DATA + __got,__data  */
    lc_size += 72;  ncmds++;   /* LC_SEGMENT_64 __LINKEDIT (no sections)  */
    lc_size += 48;  ncmds++;   /* LC_DYLD_INFO_ONLY */
    lc_size += 24;  ncmds++;   /* LC_SYMTAB   */
    lc_size += 80;  ncmds++;   /* LC_DYSYMTAB */
    const char *dylinker = "/usr/lib/dyld";
    int dylinker_cmd_sz = m_align_up(12 + (int)strlen(dylinker) + 1, 8);
    lc_size += dylinker_cmd_sz; ncmds++; /* LC_LOAD_DYLINKER */
    lc_size += 24;  ncmds++;   /* LC_UUID */
    lc_size += 24;  ncmds++;   /* LC_BUILD_VERSION */
    lc_size += 16;  ncmds++;   /* LC_SOURCE_VERSION */
    lc_size += 24;  ncmds++;   /* LC_MAIN */
    {
        int li = 0;
        while (li < macho_nlibs) {
            dylib_cmd_sz[li] = m_align_up(24 + (int)strlen(macho_lib_names + li*MACHO_LIB_NLEN) + 1, 8);
            lc_size += dylib_cmd_sz[li]; ncmds++;
            li++;
        }
    }

    int hdr_sz = 32; /* mach_header_64 */

    /* Entry stub, appended after the user text (see the header comment for
     * why it exists and why it goes at the end):
     *
     *   sub  rsp, 8              ; realign: LC_MAIN entry is reached by a
     *                            ; call, so rsp is 8 mod 16 on arrival and
     *                            ; must be 0 mod 16 at the next call
     *   call main                ; argc/argv/envp/apple already in rdi/rsi/rdx/rcx
     *   mov  edi, eax            ; exit status = main's return value
     *   call qword ptr [rip+got] ; exit() — never returns, which is exactly
     *                            ; what makes main's clobbered callee-saved
     *                            ; registers harmless
     *   ud2                      ; unreachable; traps loudly if exit ever does return
     */
    uint8_t stub[32];
    int stub_len = 0;
    int stub_call_off, stub_exit_off;
    stub[stub_len++]=0x48; stub[stub_len++]=0x83; stub[stub_len++]=0xEC; stub[stub_len++]=0x08;
    stub[stub_len++]=0xE8; stub_call_off = stub_len;
    stub[stub_len++]=0x00; stub[stub_len++]=0x00; stub[stub_len++]=0x00; stub[stub_len++]=0x00;
    stub[stub_len++]=0x89; stub[stub_len++]=0xC7;
    stub[stub_len++]=0xFF; stub[stub_len++]=0x15; stub_exit_off = stub_len;
    stub[stub_len++]=0x00; stub[stub_len++]=0x00; stub[stub_len++]=0x00; stub[stub_len++]=0x00;
    stub[stub_len++]=0x0F; stub[stub_len++]=0x0B;
    int stub_base = in->text_len;

    /* __TEXT: header + load commands + code + read-only pool. */
    int off_text  = m_align_up(hdr_sz + lc_size, 16);
    int text_len  = in->text_len + stub_len;
    int off_const = m_align_up(off_text + text_len, 16);
    int const_len = in->rdata_strings_len;
    int text_seg_filesize = m_align_up(off_const + const_len, MACHO_PAGE);

    /* __DATA: GOT then writable globals. */
    int data_seg_off = text_seg_filesize;
    int off_got = data_seg_off;
    int got_sz  = nfuncs * 8;
    int off_data = m_align_up(off_got + got_sz, 16);
    int data_sz  = in->wdata_len;
    int data_seg_filesize = m_align_up((off_data + data_sz) - data_seg_off, MACHO_PAGE);
    if (data_seg_filesize == 0) data_seg_filesize = MACHO_PAGE;

    /* __LINKEDIT: everything dyld reads but nothing the program addresses. */
    int linkedit_off = data_seg_off + data_seg_filesize;

    /* Now that __got's position is known, emit the bind stream (2d above). */
    {
        int got_off_in_seg = off_got - data_seg_off;
        m_pu8(bind, &bind_len, (uint8_t)(BIND_OPCODE_SET_TYPE_IMM | BIND_TYPE_POINTER));
        m_pu8(bind, &bind_len, (uint8_t)(BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | MACHO_DATA_SEG_INDEX));
        m_uleb(bind, &bind_len, (uint32_t)got_off_in_seg);
        int cur_ord = -1;
        int fi = 0;
        while (fi < nfuncs) {
            int ord = macho_func_lib[fi] + 1; /* ordinals are 1-based */
            if (ord != cur_ord) {
                m_pu8(bind, &bind_len, (uint8_t)(BIND_OPCODE_SET_DYLIB_ORDINAL_IMM | (ord & 0x0F)));
                cur_ord = ord;
            }
            m_pu8(bind, &bind_len, (uint8_t)(BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM | 0));
            m_pu8(bind, &bind_len, (uint8_t)'_');
            { const char *nm = macho_func_names + fi * MACHO_FUNC_NLEN;
              int k = 0; while (nm[k]) { m_pu8(bind, &bind_len, (uint8_t)nm[k]); k++; } }
            m_pu8(bind, &bind_len, 0);
            /* DO_BIND writes the current slot and advances the cursor by one
             * pointer, so consecutive GOT slots need no further SET_SEGMENT. */
            m_pu8(bind, &bind_len, BIND_OPCODE_DO_BIND);
            fi++;
        }
        m_pu8(bind, &bind_len, BIND_OPCODE_DONE);
    }
    int bind_padded = m_align_up(bind_len, 8);

    int off_bind     = linkedit_off;
    int off_symtab   = off_bind + bind_padded;
    int off_indirect = off_symtab + symtab_sz;
    int off_strtab   = off_indirect + m_align_up(indirect_sz, 8);
    int linkedit_end = off_strtab + strtab_padded;
    int linkedit_filesize = m_align_up(linkedit_end - linkedit_off, MACHO_PAGE);

    /* Virtual addresses are simply the image base plus the file offset (see
     * the non-PIE note at the top), so the low half of any address is its
     * own file offset and only the high half is constant. Relocation math
     * below works purely in these offsets, since the base cancels out of
     * every RIP-relative difference. */
    int total_size = linkedit_end;

    /* ---- Step 4: entry point ----
     * inject_entry_reloc() (compiler.c) records main's offset within the
     * user text as the addend of a "__entry__" pseudo-relocation. With no
     * stub prepended, that offset is already main's offset in __text. */
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
    /* LC_MAIN's entryoff is a file offset relative to the start of __TEXT,
     * which is the start of the file. Entry is the stub, not main itself. */
    int entry_off = off_text + stub_base;

    if (getenv("SQUASH_MACHO_DEBUG"))
        fprintf(stderr, "[macho] off_text=%d text_len=%d off_const=%d const_len=%d off_got=%d got_sz=%d off_data=%d data_sz=%d entry_off=%d main_user_off=%d\n",
                off_text, text_len, off_const, const_len, off_got, got_sz, off_data, data_sz, entry_off, main_user_off);

    /* Combined text = user code ++ entry stub. */
    uint8_t *text = (uint8_t *)malloc((size_t)(text_len > 0 ? text_len : 1));
    { int si = 0; while (si < in->text_len) { text[si] = in->text[si]; si++; }
      si = 0; while (si < stub_len) { text[stub_base + si] = stub[si]; si++; } }

    /* Patch the stub's two displacements now that the layout is fixed. */
    {
        int call_site = stub_base + stub_call_off;
        m_patch32(text, call_site, (uint32_t)(int32_t)(main_user_off - (call_site + 4)));

        int exit_slot = macho_grp_got_idx("exit");
        if (exit_slot < 0) {
            diag_emit(DIAG_ERROR, -1, NULL, NULL, "internal: exit() import missing from the Mach-O GOT");
            free(text); free(bind); free(symtab); free(indirect); free(strtab); free(sym_name_off);
            return 1;
        }
        int exit_site = stub_base + stub_exit_off;
        int target = off_got + exit_slot * 8;
        int pc     = off_text + exit_site + 4;
        m_patch32(text, exit_site, (uint32_t)(int32_t)(target - pc));
    }

    /* ---- Step 5: patch relocations in .text ----
     * A direct port of elf_builder.c's Step 9, minus the "+ stub_len" that
     * every offset needs there and minus the AArch64 cases (this backend is
     * x86-64 only). Displacements are computed from file offsets because
     * the shared image base cancels in every subtraction. */
    {
        int ri = 0;
        while (ri < in->reloc_count) {
            Relocation *r = &in->relocs[ri];
            if (!r->symbol) { ri++; continue; }
            if (strcmp(r->symbol, "__entry__") == 0) { ri++; continue; }

            int patch_off = r->offset;
            if (patch_off < 0 || patch_off + 4 > text_len) { ri++; continue; }

            if (r->kind == RELOC_IAT_REL32) {
                int slot = macho_grp_got_idx(r->symbol);
                if (slot < 0) { ri++; continue; }
                int target = off_got + slot * 8;
                int pc     = off_text + patch_off + 4;
                m_patch32(text, patch_off, (uint32_t)(int32_t)(target - pc));

            } else if (r->kind == RELOC_DATA_REL32) {
                int slab_off = -1;
                int si = 0;
                while (si < in->string_count) {
                    if (strcmp(in->string_labels[si], r->symbol) == 0) { slab_off = in->string_offsets[si]; break; }
                    si++;
                }
                if (slab_off < 0) { ri++; continue; }
                int target = off_const + slab_off;
                int pc     = off_text + patch_off + 4;
                m_patch32(text, patch_off, (uint32_t)(int32_t)(target - pc));

            } else if (r->kind == RELOC_WDATA_REL32) {
                int wlab_off = -1;
                int wi = 0;
                while (wi < in->wdata_count) {
                    if (in->wdata_labels[wi] && strcmp(in->wdata_labels[wi], r->symbol) == 0) { wlab_off = in->wdata_offsets[wi]; break; }
                    wi++;
                }
                if (wlab_off < 0) {
                    /* Mirrors elf_builder.c: an unresolvable label leaves the
                     * 4-byte placeholder as-is rather than aborting the build.
                     * That is silent by design there, but it means the program
                     * dereferences a near-null address at runtime, so surface
                     * it under the debug switch — it is the first thing worth
                     * checking when a -macos binary faults inside a memcpy or
                     * an array access. */
                    if (getenv("SQUASH_MACHO_DEBUG"))
                        fprintf(stderr, "[macho] UNRESOLVED wdata label '%s' at text+%d\n", r->symbol, patch_off);
                    ri++; continue;
                }
                int target = off_data + wlab_off;
                int pc     = off_text + patch_off + 4;
                m_patch32(text, patch_off, (uint32_t)(int32_t)(target - pc));

            } else if (r->kind == RELOC_ABS32 || r->kind == RELOC_DATA_ABS32 ||
                       r->kind == RELOC_WDATA_ABS32 || r->kind == RELOC_TEXT_ABS32) {
                /* These three carry a 32-bit ABSOLUTE virtual address and
                 * exist only for the 32-bit x86 targets, where the image
                 * base fits in 32 bits. A 64-bit macOS image is based at
                 * 0x1_0000_0000, so no address in it is representable this
                 * way -- silently truncating would produce a binary that
                 * faults at a seemingly unrelated point. Since -macos is
                 * 64-bit-only (compiler.c rejects -32 with it), reaching
                 * here means codegen emitted a form this backend doesn't
                 * model rather than a user error, so say so plainly. */
                diag_emit(DIAG_ERROR, -1, NULL, NULL,
                          "macOS/x86-64: cannot encode 32-bit absolute address relocation for '%s' (image is based above 4GB)",
                          r->symbol);

            } else if (r->kind == RELOC_STATIC_REL32) {
                /* Cross-object (".sqo") calls are resolved earlier by
                 * objfile_merge(); the only ones that survive to here come
                 * from a ".a" static archive, which needs linker.c -- ELF
                 * only, hence never populated on macOS. */
                diag_emit(DIAG_ERROR, -1, NULL, NULL,
                          "unresolved static symbol: %s", r->symbol);
            }
            ri++;
        }
    }

    if (diag_error_count() > 0) {
        free(text); free(bind); free(symtab); free(indirect); free(strtab); free(sym_name_off);
        return 1;
    }

    /* ---- Step 6: assemble the file ---- */
    uint8_t *out = (uint8_t *)calloc((size_t)total_size + 1, 1);
    int hoff = 0;

    /* mach_header_64 */
    m_pu32(out, &hoff, MH_MAGIC_64);
    m_pu32(out, &hoff, CPU_TYPE_X86_64);
    m_pu32(out, &hoff, CPU_SUBTYPE_X86_64_ALL);
    m_pu32(out, &hoff, MH_EXECUTE);
    m_pu32(out, &hoff, (uint32_t)ncmds);
    m_pu32(out, &hoff, (uint32_t)lc_size);
    /* No MH_PIE: this backend fixes the image base at link time (see the
     * header comment), which is what lets every relocation above be a plain
     * link-time constant with no rebase stream. */
    m_pu32(out, &hoff, MH_DYLDLINK | MH_TWOLEVEL);
    m_pu32(out, &hoff, 0); /* reserved */

    /* LC_SEGMENT_64 __PAGEZERO — unmapped, so a null dereference faults. */
    m_pu32(out, &hoff, LC_SEGMENT_64);
    m_pu32(out, &hoff, 72);
    m_pname16(out, &hoff, "__PAGEZERO");
    m_pu64_hl(out, &hoff, 0, 0);                                   /* vmaddr  */
    m_pu64_hl(out, &hoff, MACHO_IMAGE_BASE_HI, MACHO_IMAGE_BASE_LO); /* vmsize */
    m_pu64_hl(out, &hoff, 0, 0);   /* fileoff  */
    m_pu64_hl(out, &hoff, 0, 0);   /* filesize */
    m_pu32(out, &hoff, 0);         /* maxprot  */
    m_pu32(out, &hoff, 0);         /* initprot */
    m_pu32(out, &hoff, 0);         /* nsects   */
    m_pu32(out, &hoff, 0);         /* flags    */

    /* LC_SEGMENT_64 __TEXT — must start at file offset 0 so that it covers
     * the Mach header and load commands themselves; dyld requires this. */
    m_pu32(out, &hoff, LC_SEGMENT_64);
    m_pu32(out, &hoff, (uint32_t)(72 + 2*80));
    m_pname16(out, &hoff, "__TEXT");
    m_pu64_hl(out, &hoff, MACHO_IMAGE_BASE_HI, MACHO_IMAGE_BASE_LO);
    m_pu64_hl(out, &hoff, 0, (uint32_t)text_seg_filesize); /* vmsize   */
    m_pu64_hl(out, &hoff, 0, 0);                           /* fileoff  */
    m_pu64_hl(out, &hoff, 0, (uint32_t)text_seg_filesize); /* filesize */
    m_pu32(out, &hoff, VM_PROT_READ | VM_PROT_EXEC);       /* maxprot  */
    m_pu32(out, &hoff, VM_PROT_READ | VM_PROT_EXEC);       /* initprot */
    m_pu32(out, &hoff, 2);
    m_pu32(out, &hoff, 0);
    /*   section __TEXT,__text */
    m_pname16(out, &hoff, "__text");
    m_pname16(out, &hoff, "__TEXT");
    m_pu64_hl(out, &hoff, MACHO_IMAGE_BASE_HI, (uint32_t)off_text); /* addr */
    m_pu64_hl(out, &hoff, 0, (uint32_t)text_len);                   /* size */
    m_pu32(out, &hoff, (uint32_t)off_text); /* offset */
    m_pu32(out, &hoff, 4);                  /* align 2^4 */
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0); /* reloff/nreloc */
    m_pu32(out, &hoff, 0x80000400u);        /* S_ATTR_PURE_INSTRUCTIONS|S_ATTR_SOME_INSTRUCTIONS */
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);
    /*   section __TEXT,__const — string literals AND float constants share
     *   this pool, so it is a plain S_REGULAR section rather than
     *   __cstring/S_CSTRING_LITERALS (which would promise every entry is a
     *   NUL-terminated string, and the float constants are not). */
    m_pname16(out, &hoff, "__const");
    m_pname16(out, &hoff, "__TEXT");
    m_pu64_hl(out, &hoff, MACHO_IMAGE_BASE_HI, (uint32_t)off_const);
    m_pu64_hl(out, &hoff, 0, (uint32_t)const_len);
    m_pu32(out, &hoff, (uint32_t)off_const);
    m_pu32(out, &hoff, 4);
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);
    m_pu32(out, &hoff, 0); /* S_REGULAR */
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);

    /* LC_SEGMENT_64 __DATA */
    m_pu32(out, &hoff, LC_SEGMENT_64);
    m_pu32(out, &hoff, (uint32_t)(72 + 2*80));
    m_pname16(out, &hoff, "__DATA");
    m_pu64_hl(out, &hoff, MACHO_IMAGE_BASE_HI, (uint32_t)data_seg_off);
    m_pu64_hl(out, &hoff, 0, (uint32_t)data_seg_filesize);
    m_pu64_hl(out, &hoff, 0, (uint32_t)data_seg_off);
    m_pu64_hl(out, &hoff, 0, (uint32_t)data_seg_filesize);
    m_pu32(out, &hoff, VM_PROT_READ | VM_PROT_WRITE);
    m_pu32(out, &hoff, VM_PROT_READ | VM_PROT_WRITE);
    m_pu32(out, &hoff, 2);
    m_pu32(out, &hoff, 0);
    /*   section __DATA,__got — the import table. S_NON_LAZY_SYMBOL_POINTERS
     *   plus reserved1 (the index of this section's first entry in the
     *   indirect symbol table) is what ties each 8-byte slot back to its
     *   undefined symbol. */
    m_pname16(out, &hoff, "__got");
    m_pname16(out, &hoff, "__DATA");
    m_pu64_hl(out, &hoff, MACHO_IMAGE_BASE_HI, (uint32_t)off_got);
    m_pu64_hl(out, &hoff, 0, (uint32_t)got_sz);
    m_pu32(out, &hoff, (uint32_t)off_got);
    m_pu32(out, &hoff, 3); /* align 2^3 = 8 */
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);
    m_pu32(out, &hoff, S_NON_LAZY_SYMBOL_POINTERS);
    m_pu32(out, &hoff, 0); /* reserved1: first indirect symbol index */
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);
    /*   section __DATA,__data */
    m_pname16(out, &hoff, "__data");
    m_pname16(out, &hoff, "__DATA");
    m_pu64_hl(out, &hoff, MACHO_IMAGE_BASE_HI, (uint32_t)off_data);
    m_pu64_hl(out, &hoff, 0, (uint32_t)data_sz);
    m_pu32(out, &hoff, (uint32_t)off_data);
    m_pu32(out, &hoff, 4);
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);
    m_pu32(out, &hoff, 0);
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);

    /* LC_SEGMENT_64 __LINKEDIT — must be the last segment in the file. */
    m_pu32(out, &hoff, LC_SEGMENT_64);
    m_pu32(out, &hoff, 72);
    m_pname16(out, &hoff, "__LINKEDIT");
    m_pu64_hl(out, &hoff, MACHO_IMAGE_BASE_HI, (uint32_t)linkedit_off);
    m_pu64_hl(out, &hoff, 0, (uint32_t)linkedit_filesize);
    m_pu64_hl(out, &hoff, 0, (uint32_t)linkedit_off);
    m_pu64_hl(out, &hoff, 0, (uint32_t)(linkedit_end - linkedit_off));
    m_pu32(out, &hoff, VM_PROT_READ);
    m_pu32(out, &hoff, VM_PROT_READ);
    m_pu32(out, &hoff, 0);
    m_pu32(out, &hoff, 0);

    /* LC_DYLD_INFO_ONLY — bind only. Nothing to rebase (non-PIE), no weak
     * or lazy binding (every import is bound eagerly at load, the direct
     * equivalent of the ELF path's DF_BIND_NOW), and nothing exported. */
    m_pu32(out, &hoff, LC_DYLD_INFO_ONLY);
    m_pu32(out, &hoff, 48);
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);                       /* rebase */
    m_pu32(out, &hoff, (uint32_t)off_bind); m_pu32(out, &hoff, (uint32_t)bind_len);
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);                       /* weak bind */
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);                       /* lazy bind */
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);                       /* export */

    /* LC_SYMTAB */
    m_pu32(out, &hoff, LC_SYMTAB);
    m_pu32(out, &hoff, 24);
    m_pu32(out, &hoff, (uint32_t)off_symtab);
    m_pu32(out, &hoff, (uint32_t)nsyms);
    m_pu32(out, &hoff, (uint32_t)off_strtab);
    m_pu32(out, &hoff, (uint32_t)strtab_padded);

    /* LC_DYSYMTAB — every symbol we emit is undefined, so the local and
     * external-defined ranges are empty and the undefined range is all of
     * them. */
    m_pu32(out, &hoff, LC_DYSYMTAB);
    m_pu32(out, &hoff, 80);
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);                 /* ilocalsym / nlocalsym   */
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);                 /* iextdefsym / nextdefsym */
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, (uint32_t)nsyms);   /* iundefsym / nundefsym   */
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);                 /* toc      */
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);                 /* modtab   */
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);                 /* extrefsym*/
    m_pu32(out, &hoff, (uint32_t)off_indirect); m_pu32(out, &hoff, (uint32_t)nfuncs);
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);                 /* extrel   */
    m_pu32(out, &hoff, 0); m_pu32(out, &hoff, 0);                 /* locrel   */

    /* LC_LOAD_DYLINKER */
    m_pu32(out, &hoff, LC_LOAD_DYLINKER);
    m_pu32(out, &hoff, (uint32_t)dylinker_cmd_sz);
    m_pu32(out, &hoff, 12); /* name offset within the command */
    { int k = 0; while (dylinker[k]) { m_pu8(out, &hoff, (uint8_t)dylinker[k]); k++; }
      while (hoff % 8) m_pu8(out, &hoff, 0); }

    /* LC_UUID */
    m_pu32(out, &hoff, LC_UUID);
    m_pu32(out, &hoff, 24);
    { int k = 0; while (k < 16) { m_pu8(out, &hoff, (uint8_t)(0x51 + k)); k++; } }

    /* LC_BUILD_VERSION — modern dyld wants to know the target platform and
     * minimum OS; without it a binary can be rejected outright. Versions
     * are packed as xxxx.yy.zz in nibble groups (0x000A0F00 = 10.15.0). */
    m_pu32(out, &hoff, LC_BUILD_VERSION);
    m_pu32(out, &hoff, 24);
    m_pu32(out, &hoff, 1);          /* PLATFORM_MACOS */
    m_pu32(out, &hoff, 0x000A0F00); /* minos 10.15 */
    m_pu32(out, &hoff, 0x000E0000); /* sdk   14.0  */
    m_pu32(out, &hoff, 0);          /* ntools */

    /* LC_SOURCE_VERSION */
    m_pu32(out, &hoff, LC_SOURCE_VERSION);
    m_pu32(out, &hoff, 16);
    m_pu64_hl(out, &hoff, 0, 0);

    /* LC_MAIN — hands main's file offset to dyld. libSystem's start() then
     * calls it with (argc, argv, envp, apple) on a correctly aligned stack
     * and passes the return value to exit(), which is what flushes stdio.
     * This is the whole reason no _start stub is needed here. */
    m_pu32(out, &hoff, LC_MAIN);
    m_pu32(out, &hoff, 24);
    m_pu64_hl(out, &hoff, 0, (uint32_t)entry_off); /* entryoff  */
    m_pu64_hl(out, &hoff, 0, 0);                   /* stacksize: default */

    /* LC_LOAD_DYLIB per dylib, in ordinal order (ordinal = index + 1, which
     * is what the bind stream and each symbol's n_desc refer to). */
    {
        int li = 0;
        while (li < macho_nlibs) {
            const char *path = macho_lib_names + li * MACHO_LIB_NLEN;
            m_pu32(out, &hoff, LC_LOAD_DYLIB);
            m_pu32(out, &hoff, (uint32_t)dylib_cmd_sz[li]);
            m_pu32(out, &hoff, 24);         /* name offset */
            m_pu32(out, &hoff, 2);          /* timestamp */
            m_pu32(out, &hoff, 0x00010000); /* current_version 1.0.0 */
            m_pu32(out, &hoff, 0x00010000); /* compatibility_version 1.0.0 */
            { int k = 0; while (path[k]) { m_pu8(out, &hoff, (uint8_t)path[k]); k++; }
              while (hoff % 8) m_pu8(out, &hoff, 0); }
            li++;
        }
    }

    if (hoff != hdr_sz + lc_size) {
        /* A mismatch means a load command's declared cmdsize disagrees with
         * what was actually written -- dyld would walk the command list off
         * the rails. Catch it here rather than shipping the file. */
        diag_emit(DIAG_ERROR, -1, NULL, NULL,
                  "internal: Mach-O load command size mismatch (wrote %d, declared %d)",
                  hoff, hdr_sz + lc_size);
        free(out); free(text); free(bind); free(symtab); free(indirect); free(strtab); free(sym_name_off);
        return 1;
    }

    /* Section payloads */
    { int i = 0; while (i < text_len)  { out[off_text  + i] = text[i]; i++; } }
    { int i = 0; while (i < const_len) { out[off_const + i] = in->rdata_strings[i]; i++; } }
    if (in->wdata_bytes) { int i = 0; while (i < data_sz) { out[off_data + i] = in->wdata_bytes[i]; i++; } }
    /* __got starts zeroed; dyld overwrites every slot from the bind stream. */

    /* __LINKEDIT payloads */
    { int i = 0; while (i < bind_len)     { out[off_bind     + i] = bind[i];     i++; } }
    { int i = 0; while (i < symtab_sz)    { out[off_symtab   + i] = symtab[i];   i++; } }
    { int i = 0; while (i < indirect_sz)  { out[off_indirect + i] = indirect[i]; i++; } }
    { int i = 0; while (i < strtab_len)   { out[off_strtab   + i] = strtab[i];   i++; } }

    /* ---- Step 7: write it out ---- */
    FILE *fp = fopen(in->output_path, "wb");
    if (!fp) {
        diag_emit(DIAG_ERROR, -1, NULL, NULL, "cannot write %s", in->output_path);
        free(out); free(text); free(bind); free(symtab); free(indirect); free(strtab); free(sym_name_off);
        return 1;
    }
    fwrite(out, 1, (size_t)total_size, fp);
    fclose(fp);
    chmod(in->output_path, 0755);

    printf("Wrote Mach-O executable: %s (%d bytes, %d import%s from %d dylib%s)\n",
           in->output_path, total_size,
           nfuncs, nfuncs==1?"":"s", macho_nlibs, macho_nlibs==1?"":"s");

    free(out); free(text); free(bind); free(symtab); free(indirect); free(strtab); free(sym_name_off);
    return 0;
}
