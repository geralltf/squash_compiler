#ifndef IMPLIB_H
#define IMPLIB_H

/* Looks up which DLL exports a given symbol by parsing real Windows SDK
 * .lib import libraries (COFF archives of short-format IMPORT_OBJECT
 * records) — see implib.c for format details. Returns a DLL name string
 * (e.g. "KERNEL32.dll") owned by this module, or NULL if the symbol isn't
 * found in any loaded import library (including when the SDK isn't
 * installed at all, or none of the well-known .lib files were found —
 * this is a pure fallback layered on top of symtable.c's own hardcoded
 * name table, never required for existing behavior to keep working). */
const char *implib_find_dll(const char *symbol_name);

#endif
