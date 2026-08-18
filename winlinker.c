#include "winlinker.h"
#include "symtable.h"
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *my_strdup(const char *src);

/* =========================================================================
 * Lifecycle
 * ========================================================================= */
WinLinkerContext *winlinker_new(int is_64bit) {
    WinLinkerContext *ctx = calloc(1, sizeof(WinLinkerContext));
    ctx->is_64bit = is_64bit;
    return ctx;
}

void winlinker_free(WinLinkerContext *ctx) {
    if (ctx) free(ctx);
}

/* =========================================================================
 * Configuration
 * ========================================================================= */
void winlinker_add_search_path(WinLinkerContext *ctx, const char *path) {
    if (ctx->search_count >= WINLINK_MAX_SEARCH) return;
    strncpy(ctx->search[ctx->search_count], path, 511);
    ctx->search[ctx->search_count][511] = '\0';
    ctx->search_count++;
}

void winlinker_add_library(WinLinkerContext *ctx, const char *name) {
    if (ctx->lib_count >= WINLINK_MAX_LIBS) return;
    strncpy(ctx->lib_name[ctx->lib_count], name, 511);
    ctx->lib_name[ctx->lib_count][511] = '\0';
    ctx->lib_count++;
}

/* =========================================================================
 * Internal helpers
 * ========================================================================= */
static uint16_t wl_rd16(const uint8_t *b, int off) {
    return (uint16_t)((uint32_t)b[off] | ((uint32_t)b[off+1]<<8));
}
static uint32_t wl_rd32(const uint8_t *b, int off) {
    return (uint32_t)b[off] | ((uint32_t)b[off+1]<<8) |
           ((uint32_t)b[off+2]<<16) | ((uint32_t)b[off+3]<<24);
}

static uint8_t *wl_read_file(const char *path, int *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    uint8_t *buf = malloc(sz + 1);
    fread(buf, 1, sz, f); fclose(f);
    buf[sz] = 0; *out_len = (int)sz;
    return buf;
}

static int wl_file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f); return 1;
}

/* Register a symbol → DLL mapping */
static void wl_add_sym(WinLinkerContext *ctx, const char *sym, const char *dll) {
    if (ctx->sym_count >= WINLINK_MAX_SYMS) return;
    /* Check for duplicate */
    int i;
    for (i = 0; i < ctx->sym_count; i++) {
        if (strcmp(ctx->sym_name[i], sym) == 0) return;
    }
    strncpy(ctx->sym_name[ctx->sym_count], sym, 127);
    ctx->sym_name[ctx->sym_count][127] = '\0';
    strncpy(ctx->sym_dll[ctx->sym_count], dll, 255);
    ctx->sym_dll[ctx->sym_count][255] = '\0';
    ctx->sym_count++;
}

/* =========================================================================
 * Parse a PE .dll file and extract exported function names.
 * PE format: DOS header → PE signature → COFF header → optional header
 *            → section headers → export directory.
 * ========================================================================= */
static void wl_parse_dll(WinLinkerContext *ctx, const char *path, const char *dllname) {
    int flen = 0;
    uint8_t *buf = wl_read_file(path, &flen);
    if (!buf) return;

    /* Validate MZ signature */
    if (flen < 64 || buf[0] != 'M' || buf[1] != 'Z') { free(buf); return; }

    uint32_t pe_off = wl_rd32(buf, 60);
    if ((int)pe_off + 24 > flen) { free(buf); return; }

    /* Validate PE\0\0 signature */
    if (buf[pe_off]!='P'||buf[pe_off+1]!='E'||buf[pe_off+2]!=0||buf[pe_off+3]!=0) {
        free(buf); return;
    }

    uint32_t coff_off = pe_off + 4;
    uint16_t num_secs  = wl_rd16(buf, coff_off + 2);
    uint16_t opt_size  = wl_rd16(buf, coff_off + 16);
    uint16_t magic     = wl_rd16(buf, coff_off + 20); /* optional header magic */

    uint32_t sec_start = coff_off + 20 + opt_size;

    /* Find export directory RVA from optional header data directory */
    uint32_t exp_rva = 0, exp_size = 0;
    if (magic == 0x010B && opt_size >= 96) {
        /* PE32: data directories at opt_off + 96 */
        exp_rva  = wl_rd32(buf, coff_off + 20 + 96);
        exp_size = wl_rd32(buf, coff_off + 20 + 100);
    } else if (magic == 0x020B && opt_size >= 112) {
        /* PE32+: data directories at opt_off + 112 */
        exp_rva  = wl_rd32(buf, coff_off + 20 + 112);
        exp_size = wl_rd32(buf, coff_off + 20 + 116);
    }
    if (exp_rva == 0 || exp_size == 0) { free(buf); return; }

    /* Convert export directory RVA to file offset via section table */
    uint32_t exp_foff = 0;
    int si;
    for (si = 0; si < (int)num_secs; si++) {
        int soff = (int)sec_start + si * 40;
        if (soff + 40 > flen) break;
        uint32_t vaddr  = wl_rd32(buf, soff + 12);
        uint32_t vsz    = wl_rd32(buf, soff + 16);
        uint32_t rawptr = wl_rd32(buf, soff + 20);
        if (exp_rva >= vaddr && exp_rva < vaddr + vsz) {
            exp_foff = rawptr + (exp_rva - vaddr);
            break;
        }
    }
    if (exp_foff == 0 || (int)exp_foff + 40 > flen) { free(buf); return; }

    /* Export directory structure */
    uint32_t num_funcs = wl_rd32(buf, exp_foff + 20);
    uint32_t num_names = wl_rd32(buf, exp_foff + 24);
    uint32_t names_rva = wl_rd32(buf, exp_foff + 32);

    if (num_names == 0) { free(buf); return; }

    /* Convert names array RVA to file offset */
    uint32_t names_foff = 0;
    for (si = 0; si < (int)num_secs; si++) {
        int soff = (int)sec_start + si * 40;
        if (soff + 40 > flen) break;
        uint32_t vaddr  = wl_rd32(buf, soff + 12);
        uint32_t vsz    = wl_rd32(buf, soff + 16);
        uint32_t rawptr = wl_rd32(buf, soff + 20);
        if (names_rva >= vaddr && names_rva < vaddr + vsz) {
            names_foff = rawptr + (names_rva - vaddr);
            break;
        }
    }
    if (names_foff == 0) { free(buf); return; }

    /* Walk the name pointer array, convert each name RVA to a file offset */
    uint32_t ni;
    for (ni = 0; ni < num_names && (int)(names_foff + ni*4 + 4) <= flen; ni++) {
        uint32_t name_rva = wl_rd32(buf, names_foff + ni * 4);
        /* Convert name RVA to file offset */
        uint32_t name_foff = 0;
        for (si = 0; si < (int)num_secs; si++) {
            int soff = (int)sec_start + si * 40;
            if (soff + 40 > flen) break;
            uint32_t vaddr  = wl_rd32(buf, soff + 12);
            uint32_t vsz    = wl_rd32(buf, soff + 16);
            uint32_t rawptr = wl_rd32(buf, soff + 20);
            if (name_rva >= vaddr && name_rva < vaddr + vsz) {
                name_foff = rawptr + (name_rva - vaddr);
                break;
            }
        }
        if (name_foff == 0 || (int)name_foff >= flen) continue;
        /* Extract null-terminated function name */
        char fname[128]; int fi = 0;
        while ((int)(name_foff + fi) < flen && buf[name_foff + fi] && fi < 127) {
            fname[fi] = (char)buf[name_foff + fi]; fi++;
        }
        fname[fi] = '\0';
        if (fi > 0) wl_add_sym(ctx, fname, dllname);
    }

    free(buf);
}

/* =========================================================================
 * Parse a COFF import library (.lib) to extract symbol → DLL mappings.
 *
 * Format: COFF archive ("!<arch>\n") followed by members.
 * Import lib members have a short COFF import header (0x0000 machine word)
 * followed by symbol name and DLL name.
 * ========================================================================= */
static void wl_parse_lib(WinLinkerContext *ctx, const char *path) {
    int flen = 0;
    uint8_t *buf = wl_read_file(path, &flen);
    if (!buf) return;

    /* Validate COFF archive signature */
    if (flen < 8 || strncmp((char*)buf, "!<arch>\n", 8) != 0) {
        free(buf); return;
    }

    int pos = 8;
    while (pos + 60 <= flen) {
        /* AR member header: 60 bytes */
        /* Name: [0..15], Date: [16..27], UID: [28..33], GID: [34..39],
         * Mode: [40..47], Size: [48..57], End: [58..59] = "`\n" */
        char size_str[11]; strncpy(size_str, (char*)buf + pos + 48, 10); size_str[10] = '\0';
        int member_size = atoi(size_str);
        int data_start = pos + 60;
        if (data_start + member_size > flen) break;

        /* COFF import header: if first two bytes are 0x00 0x00, it's an import stub */
        if (member_size >= 20 && buf[data_start]==0x00 && buf[data_start+1]==0x00) {
            /* Short import header layout:
             * uint16 Sig1=0x0000, Sig2=0xFFFF, Version, Machine
             * uint32 TimeDateStamp, SizeOfData
             * uint16 OrdinalOrHint, Type(bits 0-1) NameType(bits 2-4)
             * Then: NUL-terminated symbol name + NUL-terminated DLL name */
            /* Validate Sig2 */
            if (wl_rd16(buf, data_start + 2) == 0xFFFF && member_size > 20) {
                int str_off = data_start + 20;
                /* Symbol name */
                char sym[128]; int si2 = 0;
                while (str_off + si2 < flen && buf[str_off + si2] && si2 < 127) {
                    sym[si2] = (char)buf[str_off + si2]; si2++;
                }
                sym[si2] = '\0';
                /* DLL name follows after symbol name + NUL */
                int dll_off = str_off + si2 + 1;
                char dll[256]; int di = 0;
                while (dll_off + di < flen && buf[dll_off + di] && di < 255) {
                    dll[di] = (char)buf[dll_off + di]; di++;
                }
                dll[di] = '\0';
                if (si2 > 0 && di > 0) {
                    /* Strip leading underscore for __cdecl names */
                    const char *sname = sym;
                    if (sname[0] == '_') sname++;
                    wl_add_sym(ctx, sname, dll);
                }
            }
        }

        /* Advance to next member (align to even byte boundary) */
        pos = data_start + member_size;
        if (pos & 1) pos++;
    }

    free(buf);
}

/* =========================================================================
 * Locate a library file in the search path.
 * Tries: direct path, path/NAME.dll, path/NAME.lib, path/libNAME.dll, path/libNAME.lib
 * ========================================================================= */
static int wl_find_lib(WinLinkerContext *ctx, const char *name,
                       char *out_path, int out_size, int *is_dll) {
    /* Check if it's a direct file path */
    if (wl_file_exists(name)) {
        strncpy(out_path, name, out_size - 1); out_path[out_size-1] = '\0';
        int nl = (int)strlen(name);
        *is_dll = (nl > 4 && strcmp(name + nl - 4, ".dll") == 0);
        return 1;
    }

    int si;
    for (si = -1; si < ctx->search_count; si++) {
        const char *base = (si < 0) ? "." : ctx->search[si];
        char candidates[4][512];
        snprintf(candidates[0], 512, "%s/%s.dll",    base, name);
        snprintf(candidates[1], 512, "%s/%s.lib",    base, name);
        snprintf(candidates[2], 512, "%s/lib%s.dll", base, name);
        snprintf(candidates[3], 512, "%s/lib%s.lib", base, name);
        int ci;
        for (ci = 0; ci < 4; ci++) {
            if (wl_file_exists(candidates[ci])) {
                strncpy(out_path, candidates[ci], out_size - 1);
                out_path[out_size-1] = '\0';
                *is_dll = (ci == 0 || ci == 2);
                return 1;
            }
        }
    }
    return 0;
}

/* =========================================================================
 * Resolve: load all requested libraries and populate sym_name/sym_dll.
 * ========================================================================= */
void winlinker_resolve(WinLinkerContext *ctx) {
    int li;
    for (li = 0; li < ctx->lib_count; li++) {
        char path[1024]; int is_dll = 0;
        if (!wl_find_lib(ctx, ctx->lib_name[li], path, sizeof path, &is_dll)) {
            diag_emit(DIAG_WARNING, -1, NULL, NULL,
                      "library '%s' not found -- any symbol expected from it will show up as an unresolved import instead",
                      ctx->lib_name[li]);
            continue;
        }
        /* Extract DLL base name for use in import entries */
        char dllname[256];
        const char *base = path;
        const char *cp;
        for (cp = path; *cp; cp++) if (*cp=='/'||*cp=='\\') base=cp+1;
        strncpy(dllname, base, 255); dllname[255] = '\0';
        if (is_dll) {
            wl_parse_dll(ctx, path, dllname);
        } else {
            wl_parse_lib(ctx, path);
        }
    }
}

/* =========================================================================
 * Query: given function name, return DLL name (or NULL)
 * ========================================================================= */
const char *winlinker_lookup_dll(WinLinkerContext *ctx, const char *funcname) {
    int i;
    for (i = 0; i < ctx->sym_count; i++) {
        if (strcmp(ctx->sym_name[i], funcname) == 0)
            return ctx->sym_dll[i];
    }
    return NULL;
}

/* =========================================================================
 * Inject resolved imports into the symbol table so pe_builder generates
 * the correct IAT entries for user-provided DLLs.
 * ========================================================================= */
void winlinker_inject_imports(WinLinkerContext *ctx, void *symtable_ptr) {
    SymTable *sym = (SymTable*)symtable_ptr;
    int i;
    for (i = 0; i < ctx->sym_count; i++) {
        /* Only inject if not already in symtable */
        Symbol *existing = symtable_lookup(sym, ctx->sym_name[i]);
        if (!existing) {
            symtable_define_import(sym, ctx->sym_name[i], ctx->sym_dll[i]);
        }
    }
}
