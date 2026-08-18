#ifndef ELF_BUILDER_H
#define ELF_BUILDER_H

#include <stdint.h>
#include "assembler.h"
#include "linker.h"

/* =========================================================================
 * ELF build input — mirrors PEBuildInput for Linux ELF output
 * ========================================================================= */
typedef struct {
    int is_64bit;
    int is_arm64;   /* 1 = AArch64 target (implies is_64bit=1) */
    /* 1 = OpenBSD target (implies is_64bit=1 unless -arm64/-32 combo used).
     * OpenBSD's kernel refuses to exec a native ELF binary that isn't
     * branded as one (EI_OSABI + a PT_NOTE .note.openbsd.ident segment),
     * unlike Linux which execs any SysV ELF regardless of branding — see
     * the is_openbsd branches in elf_link_and_write(). */
    int is_openbsd;

    /* .text section: raw machine code from assembler */
    uint8_t *text;
    int      text_len;

    /* .rodata string pool (read-only) */
    uint8_t *rdata_strings;
    int      rdata_strings_len;

    /* .data writable pool */
    uint8_t *wdata_bytes;
    int      wdata_len;
    char   **wdata_labels;
    int     *wdata_offsets;
    int      wdata_count;

    /* Relocations to patch */
    Relocation *relocs;
    int         reloc_count;

    /* String labels: name -> offset within rdata_strings */
    char **string_labels;
    int   *string_offsets;
    int    string_count;

    /* Imports: "libname.so.N:funcname" pairs */
    char **import_specs;
    int    import_count;

    /* Entry point function name */
    char *entry_func;

    /* Output file path */
    const char *output_path;

    /* Assembler — needed to resolve RELOC_TEXT_ABS32 (function pointer addresses) */
    Assembler *as_;

    /* Linker context — provides static text, static rodata, static relocs,
     * and extra DT_NEEDED entries for -l libraries (Linux only).
     * NULL if no user libraries were specified.                              */
    LinkerContext *linker;
} ELFBuildInput;

/* Build and write the ELF executable */
int elf_link_and_write(ELFBuildInput *in);

#endif /* ELF_BUILDER_H */
