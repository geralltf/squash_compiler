#ifndef OBJFILE_H
#define OBJFILE_H
#include <stdint.h>
#include "assembler.h"

/* =========================================================================
 * squash object files (".sqo") — a minimal, squash-native intermediate
 * format enabling separate compilation: "squash -c big_common.c -o
 * common.sqo" compiles the shared/expensive part of a program ONCE, and
 * "squash small_main.c common.sqo -o final.exe" recompiles only the small,
 * frequently-changing part and links it against the cached object instead
 * of recompiling everything from source every time.
 *
 * This is NOT a real COFF/ELF object format — squash already has its own
 * from-scratch PE/ELF *executable* builders (pe_builder.c/elf_builder.c),
 * and inventing a matching real object-file format on top would be a much
 * larger undertaking for no benefit here, since nothing outside squash
 * itself needs to consume a ".sqo" file. It stores exactly the arrays
 * compiler.c already gathers from a completed CodeGen/Assembler pass
 * (see codegen_get_text/get_rdata/get_relocs and the wdata/string label
 * tables in compiler.c's main()), plus a function EXPORT table (name ->
 * .text offset, for every function compiled here that has a real body) so
 * a later link step can resolve cross-object calls by name.
 *
 * Scope/limitations (documented, not silently unsupported):
 *  - Only functions can be called across objects (RELOC_STATIC_REL32,
 *    resolved by objfile_merge() against the merged export table). Cross-
 *    object references to a GLOBAL VARIABLE by name are not supported yet —
 *    every object's string/float-constant and wdata (globals + static
 *    locals) labels are namespaced per-object during merge (see
 *    objfile_merge()'s own comment) specifically because they collide
 *    across separately-compiled files (codegen's "strN"/"static_X_N"
 *    label-naming scheme is only unique WITHIN one compile), which also
 *    means a name-based cross-object global reference wouldn't resolve
 *    even if attempted.
 *  - Originally Windows-PE-only; now also supported for Linux ELF (both
 *    x86-64 and arm64) — see linker.c's separate .a/.o reader for pulling
 *    in REAL external ELF objects, a related but different problem from
 *    squash's own split-compile output handled here.
 *  - Every ".sqo" file is tagged with the exact target (is_64bit/is_linux/
 *    is_arm64) it was compiled for. objfile_read() refuses to load an
 *    object whose tag doesn't match the current compile's target instead
 *    of silently merging mismatched machine code — see its own comment. */

typedef struct {
    int       is_64bit;
    int       is_linux;
    int       is_arm64;

    uint8_t  *text;         int text_len;
    uint8_t  *rdata;        int rdata_len;
    int       wdata_pool_size;

    Relocation *relocs;     int reloc_count;

    char    **str_labels;   int *str_offsets;   int str_count;
    char    **wdata_labels; int *wdata_offsets; int wdata_count;

    /* Functions defined (with a real body) in this object: name -> byte
     * offset within `text`. This is what objfile_merge() resolves
     * RELOC_STATIC_REL32 references against. */
    char    **export_names; int *export_offsets; int export_count;

    char    **import_specs; int import_count;   /* "DLL:func" strings */
} ObjFile;

/* Write an object file. All arrays are borrowed (copied into the file, not
 * retained) — caller keeps ownership and should free as usual afterward. */
int  objfile_write(const char *path,
    int is_64bit, int is_linux, int is_arm64,
    const uint8_t *text, int text_len,
    const uint8_t *rdata, int rdata_len,
    int wdata_pool_size,
    const Relocation *relocs, int reloc_count,
    char **str_labels, int *str_offsets, int str_count,
    char **wdata_labels, int *wdata_offsets, int wdata_count,
    char **export_names, int *export_offsets, int export_count,
    char **import_specs, int import_count);

/* Read an object file into a freshly allocated ObjFile. Returns 1 on
 * success, 0 on failure (file missing/corrupt — prints its own error).
 * Does NOT itself check the target tag (is_64bit/is_linux/is_arm64) against
 * anything — the caller (compiler.c) knows the current compile's target and
 * is responsible for rejecting a mismatched object; see objfile_target_matches(). */
int  objfile_read(const char *path, ObjFile *out);

/* Convenience check for the caller described above: 1 if `obj`'s tagged
 * target exactly matches the given one, 0 otherwise. */
int  objfile_target_matches(const ObjFile *obj, int is_64bit, int is_linux, int is_arm64);
void objfile_free(ObjFile *obj);

/* The result of merging a freshly-compiled "main" translation unit with one
 * or more previously-compiled ObjFiles — same shape as what a single,
 * ordinary compile produces, so it can be handed straight to
 * pe_link_and_write() with no further changes there. Every cross-object
 * function call that COULD be resolved against the merged export table
 * already has its call displacement patched directly into `text`; only
 * genuinely external references (Win32 DLL imports, or a function that
 * really isn't defined anywhere in this set) remain as unresolved
 * relocations, exactly like today's single-TU output. */
typedef struct {
    uint8_t *text;  int text_len;
    uint8_t *rdata; int rdata_len;
    int      wdata_pool_size;

    Relocation *relocs; int reloc_count;

    char **str_labels;   int *str_offsets;   int str_count;
    char **wdata_labels; int *wdata_offsets; int wdata_count;

    char **import_specs; int import_count;

    /* Absolute merged-text offsets of every part's "__sqx_static_init"
     * function (see codegen.c's synthesis of that name) — one per object
     * (and the main file too, if it has no main() of its own, though that
     * shouldn't normally happen). compiler.c's final link stage calls all
     * of these, in order, before jumping to the real main() — see the long
     * comment at codegen.c's synthesis site for why this exists: a library
     * object's own global/static function-pointer initializers (e.g. SDL's
     * "static struct {...} s_mem = { real_malloc, ... };") have nowhere to
     * run without it, since the usual mechanism just prepends them into
     * main()'s body, and a "-c" object file has no main(). */
    int *static_init_offsets; int static_init_count;
} MergedOutput;

void objfile_merge(
    const uint8_t *main_text, int main_text_len,
    const uint8_t *main_rdata, int main_rdata_len,
    int main_wdata_pool_size,
    const Relocation *main_relocs, int main_reloc_count,
    char **main_str_labels, int *main_str_offsets, int main_str_count,
    char **main_wdata_labels, int *main_wdata_offsets, int main_wdata_count,
    char **main_export_names, int *main_export_offsets, int main_export_count,
    char **main_import_specs, int main_import_count,
    ObjFile *objs, int obj_count,
    MergedOutput *out);

void merged_output_free(MergedOutput *out);

#endif /* OBJFILE_H */
