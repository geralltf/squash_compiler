#ifndef WINLINKER_H
#define WINLINKER_H
#include <stdint.h>

/* =========================================================================
 * Windows static (.lib) and dynamic (.dll) library linker for squash.
 * Mirrors the Linux LinkerContext API so compiler.c can use both uniformly.
 * =========================================================================
 *
 * Design:
 *   - Parse PE export tables from .dll files to extract symbol→DLL mappings.
 *   - Parse COFF import libraries (.lib) which map symbol→DLL.
 *   - Allow `-l name` to search for name.dll or name.lib in search paths.
 *   - The resolved symbol→DLL pairs are injected into the symbol table as
 *     SYM_IMPORT entries, triggering IAT generation in pe_builder.c.
 */

#define WINLINK_MAX_SEARCH  16
#define WINLINK_MAX_LIBS    64
#define WINLINK_MAX_SYMS  4096
#define WINLINK_NAME_BUF 65536

typedef struct {
    int is_64bit;

    /* Library search paths (from -L flags) */
    char search[WINLINK_MAX_SEARCH][512];
    int  search_count;

    /* Libraries requested (from -l flags or direct .dll/.lib paths) */
    char lib_name[WINLINK_MAX_LIBS][512];
    int  lib_count;

    /* Resolved symbol → DLL mappings */
    char sym_name[WINLINK_MAX_SYMS][128];  /* function name          */
    char sym_dll [WINLINK_MAX_SYMS][256];  /* DLL name (e.g. "opengl32.dll") */
    int  sym_count;
} WinLinkerContext;

/* Lifecycle */
WinLinkerContext *winlinker_new    (int is_64bit);
void              winlinker_free   (WinLinkerContext *ctx);

/* Configuration */
void winlinker_add_search_path(WinLinkerContext *ctx, const char *path);
void winlinker_add_library    (WinLinkerContext *ctx, const char *name); /* -lNAME */

/* Resolve all libraries: load .dll/.lib files from search paths and extract symbols */
void winlinker_resolve(WinLinkerContext *ctx);

/* Query: given function name, return DLL name (or NULL if not found) */
const char *winlinker_lookup_dll(WinLinkerContext *ctx, const char *funcname);

/* Inject resolved imports into a SymTable so pe_builder sees them */
/* (caller provides SymTable* cast to void* to avoid circular header dep) */
void winlinker_inject_imports(WinLinkerContext *ctx, void *symtable_ptr);

#endif /* WINLINKER_H */
