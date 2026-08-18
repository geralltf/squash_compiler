#ifndef MACHO_BUILDER_H
#define MACHO_BUILDER_H

#include <stdint.h>
#include "assembler.h"
#include "linker.h"

/* =========================================================================
 * Mach-O build input — macOS/x86-64 sibling of ELFBuildInput (elf_builder.h)
 * and PEBuildInput (pe_builder.h).
 *
 * Deliberately field-for-field identical to ELFBuildInput: macOS on Intel
 * uses the very same System V AMD64 ABI and LP64 type model as Linux, so
 * codegen.c needs no macOS-specific paths at all — only the *container*
 * differs (Mach-O load commands instead of ELF program headers). compiler.c
 * therefore drives a -macos build through exactly the same codegen as
 * -linux and only swaps which of elf_link_and_write/macho_link_and_write
 * consumes the result.
 *
 * Scope: 64-bit x86-64 only. There is no 32-bit i386 macOS (Apple dropped
 * it in 10.15) and arm64 macOS is a separate, larger job (it requires
 * mandatory code signing and chained fixups, neither of which this backend
 * emits) — compiler.c rejects both combinations up front rather than
 * silently producing a file the loader will refuse.
 * ========================================================================= */
typedef struct {
    /* .text section: raw machine code from assembler */
    uint8_t *text;
    int      text_len;

    /* read-only pool (string literals + float constants) -> __TEXT,__const */
    uint8_t *rdata_strings;
    int      rdata_strings_len;

    /* writable pool -> __DATA,__data */
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

    /* Imports: "libname:funcname" pairs. The library half is whatever
     * codegen.c already emits for a Unix target ("libc.so.6", "libm.so.6",
     * ...); macho_dylib_path() below maps those onto the real macOS dylib
     * that actually exports the symbol, so codegen needs no changes. */
    char **import_specs;
    int    import_count;

    /* Entry point function name */
    char *entry_func;

    /* Output file path */
    const char *output_path;

    /* Assembler — kept for parity with ELFBuildInput; unused on this
     * 64-bit-only path (it exists there solely to resolve the 32-bit
     * RELOC_TEXT_ABS32 function-pointer form). */
    Assembler *as_;

    /* Linker context — always NULL today: linker.c reads ELF .so/.a files,
     * which macOS does not use. Kept so the struct stays a drop-in match
     * for ELFBuildInput. */
    LinkerContext *linker;
} MachOBuildInput;

/* Build and write the Mach-O executable. Returns 0 on success. */
int macho_link_and_write(MachOBuildInput *in);

#endif /* MACHO_BUILDER_H */
