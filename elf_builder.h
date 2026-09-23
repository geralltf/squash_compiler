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

    /* 1 = Android target: emit a bionic-ABI ET_DYN shared object (a real
     * .so, not an executable) instead of squash's usual ET_EXEC-with-
     * PT_INTERP layout. No CRT0 _start stub is generated -- there is no
     * process entry point to call, since the object is dlopen()'d by the
     * Android runtime (via android.app.NativeActivity's native glue),
     * never exec()'d. Instead, `android_export_name` (the function found
     * via __entry__, i.e. whatever in->entry_func names) is exported as a
     * DEFINED global symbol in .dynsym so dlsym() can find it, backed by
     * a minimal SysV .hash table (DT_HASH) -- bionic's linker needs one of
     * DT_HASH/DT_GNU_HASH to even know how many symbols are in .dynsym, so
     * unlike on desktop Linux it is not optional here. */
    int is_android;
    /* DT_SONAME value, e.g. "libapp.so". Defaults to "lib.so" if NULL. */
    const char *android_soname;
    /* Symbol name to export as a defined dynamic symbol. Defaults to
     * in->entry_func if NULL. */
    const char *android_export_name;

    /* 1 = omit the section-header table (.symtab/.strtab/.shstrtab and the
     * SHT describing every section) that elf_link_and_write() writes by
     * default. Those sections are pure post-program-image metadata (see
     * elf_link_and_write's own "Step 9.6" comment) -- readelf/objdump/nm/
     * gdb all need them to make sense of the binary, so they're INCLUDED
     * by default; this only exists for callers that explicitly want the
     * older, symbol-free output (a smaller file, and nothing for a
     * would-be reverse-engineer to read function names off of) -- see
     * compiler.c's "-nodebug" flag, which is the only thing that sets
     * this to 1. */
    int strip_debug_sections;

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
