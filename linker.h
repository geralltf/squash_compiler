#ifndef LINKER_H
#define LINKER_H
#include <stdint.h>

/* =========================================================================
 * Linux dynamic (.so) and static (.a) library linker for squash.
 * Windows target: ignored entirely — no-op calls.
 * ========================================================================= */

#define LINKER_MAX_SEARCH   16
#define LINKER_MAX_LIBS     16
#define LINKER_MAX_CACHE   512
#define LINKER_MAX_STATIC  512
#define LINKER_MAX_SRELOCS 8192

#define LSTR256  256
#define LSTR128  128

/* A relocation within static-linked .o code (before final VMA assignment).
 * type 0-2 are x86-64 (also used for x86-32); type 3-7 are AArch64-only
 * (only ever produced when LinkerContext.is_arm64 — see link_elf_object()
 * in linker.c). elf_builder.c's static-reloc patch step interprets `type`
 * differently depending on target arch, never both at once. */
typedef struct {
    int  offset;    /* byte offset within static_text */
    int  type;      /* x86-64: 0=PC32/PLT32  1=ABS32  2=ABS64
                      * AArch64: 3=CALL26/JUMP26 (BL/B)  4=ADRP page-hi21
                      *          5=ADD_LO12 (unscaled)  6=LDST64_LO12 (x8)
                      *          7=ABS64 (raw 8-byte field) */
    char sym[128];  /* target symbol name */
    int  addend;    /* r_addend */
} StaticReloc;

typedef struct {
    int   is_64bit;
    int   is_arm64;

    char  search[4096];
    int   search_count;

    char  lib_path  [4096];
    char  lib_soname[2048];
    int   lib_static[16];
    int   lib_count;

    char  cache_sym[65536];
    char  cache_so [65536];
    int   cache_count;

    char  ar_sym [65536];
    char  ar_file[131072];
    int   ar_off [512];
    int   ar_sym_count;

    uint8_t *static_text;
    int      static_text_len;
    int      static_text_cap;

    char  sdef_name[65536];
    int   sdef_off [512];
    int   sdef_count;

    StaticReloc *srelocs;
    int          sreloc_count;
    int          sreloc_cap;

    uint8_t *static_rdata;
    int      static_rdata_len;
    int      static_rdata_cap;

    char  srd_name  [65536];
    int   srd_offset[512];
    int   srd_count;

    char  queued[65536];
    int   queued_count;

    /* Every defined (non-SHN_UNDEF) FUNC/OBJECT symbol exported by each
     * loaded ".so", tagged with which lib_path[]/lib_soname[] index it came
     * from — populated at linker_add_lib_path() time (see so_list_exports()
     * in linker.c). Used only for the "-l/.a/.so export listing" diagnostic
     * (compiler.c), not for symbol resolution (linker_lookup_dynamic still
     * does its own, separate single-symbol lookup). */
    char  so_exp_name[65536];
    int   so_exp_lib  [512];
    int   so_exp_count;
} LinkerContext;

/* Lifecycle */
LinkerContext *linker_new    (int is_64bit, int is_arm64);
void           linker_free   (LinkerContext *ctx);

/* Configuration (call before linking) */
void linker_add_search_path(LinkerContext *ctx, const char *path);
void linker_add_library    (LinkerContext *ctx, const char *name);  /* -lNAME */
void linker_add_lib_path   (LinkerContext *ctx, const char *path);  /* direct .so/.a */

/* Queries used by codegen */
const char *linker_lookup_dynamic(LinkerContext *ctx, const char *funcname);
int         linker_link_static   (LinkerContext *ctx, const char *funcname);
int         linker_has_static_def(LinkerContext *ctx, const char *funcname);

/* ELF builder integration */
int                  linker_soname_count  (LinkerContext *ctx);
const char          *linker_soname_get    (LinkerContext *ctx, int i);
uint8_t             *linker_static_text   (LinkerContext *ctx, int *len);
uint8_t             *linker_static_rdata  (LinkerContext *ctx, int *len);
StaticReloc         *linker_static_relocs (LinkerContext *ctx, int *count);
int                  linker_static_def_count(LinkerContext *ctx);
const char          *linker_static_def_name (LinkerContext *ctx, int i);
int                  linker_static_def_off  (LinkerContext *ctx, int i);
int                  linker_static_rdata_count(LinkerContext *ctx);
const char          *linker_static_rdata_name (LinkerContext *ctx, int i);
int                  linker_static_rdata_off  (LinkerContext *ctx, int i);

/* Export-listing diagnostic support (compiler.c) — enumerate every loaded
 * -l/.a/.so input file and, for each, the symbols it exports. */
int                  linker_lib_count     (LinkerContext *ctx);
const char          *linker_lib_path      (LinkerContext *ctx, int i);
const char          *linker_lib_soname    (LinkerContext *ctx, int i);
int                  linker_lib_is_static (LinkerContext *ctx, int i);
int                  linker_so_export_count(LinkerContext *ctx);
const char          *linker_so_export_name (LinkerContext *ctx, int i);
int                  linker_so_export_lib  (LinkerContext *ctx, int i);
int                  linker_ar_sym_count  (LinkerContext *ctx);
const char          *linker_ar_sym_name   (LinkerContext *ctx, int i);
const char          *linker_ar_sym_file   (LinkerContext *ctx, int i);

#endif /* LINKER_H */
