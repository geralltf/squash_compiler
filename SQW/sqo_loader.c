#include "sqo_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* squash's own include/ has no <sys/mman.h> shim -- hand-declared here
 * rather than widening the shims (out of scope for this loader),
 * matching this project's established pattern for a missing libc
 * declaration (setjmp/longjmp in CSR/csharp_rt.h, popen/system in
 * SQW/sqw_main.c). Constant values are the standard Linux x86-64 ones
 * (stable across glibc versions, part of the kernel ABI, not glibc's
 * own choice to change). Verified this session that squash-compiled
 * code calling mmap/mprotect/munmap, and then actually EXECUTING
 * hand-placed machine code out of the mmap'd region, works correctly. */
#define SQO_PROT_READ  1
#define SQO_PROT_WRITE 2
#define SQO_PROT_EXEC  4
#define SQO_MAP_PRIVATE   0x02
#define SQO_MAP_ANONYMOUS 0x20

extern void *mmap(void *addr, unsigned long length, int prot, int flags, int fd, long offset);
extern int munmap(void *addr, unsigned long length);
extern int mprotect(void *addr, unsigned long len, int prot);

/* ---- minimal, local ".sqo" reader ----------------------------------------
 * Deliberately NOT the real objfile_read()/objfile_free() (objfile.c) --
 * see this file's own top-of-block comment in sqw_main.c for why: those
 * need diag_emit() (diag.c) and my_strdup() (ast.c), which would drag
 * squash's entire compiler-internals AST module into SQW for the sake of
 * two small helper calls. Reads exactly the same binary format objfile.h
 * documents (a simple, stable, length-prefixed layout) -- kept in sync
 * with objfile_write()/objfile_read() by hand; if that format ever
 * changes, both this and objfile.c need updating together. */
static uint32_t sqo_r_u32(FILE *f) { uint32_t v = 0; if (fread(&v, 4, 1, f) != 1) return 0; return v; }
static int32_t  sqo_r_i32(FILE *f) { int32_t v = 0; if (fread(&v, 4, 1, f) != 1) return 0; return v; }
static char *sqo_r_str(FILE *f) {
    uint32_t len = sqo_r_u32(f);
    char *s = (char *)malloc((size_t)len + 1);
    if (len) { if (fread(s, 1, len, f) != len) { s[0] = 0; return s; } }
    s[len] = 0;
    return s;
}

static int sqo_read_objfile(const char *path, ObjFile *out) {
    FILE *f;
    char magic[4];
    int i;
    memset(out, 0, sizeof(*out));
    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "sqo_loader: cannot open '%s'\n", path); return 0; }
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "SQO1", 4) != 0) {
        fprintf(stderr, "sqo_loader: '%s' is not a valid squash object file\n", path);
        fclose(f); return 0;
    }
    out->is_64bit = (int)sqo_r_u32(f);
    out->is_linux = (int)sqo_r_u32(f);
    out->is_arm64 = (int)sqo_r_u32(f);

    out->text_len = (int)sqo_r_u32(f);
    out->text = (uint8_t *)malloc((size_t)out->text_len + 1);
    if (out->text_len) { if (fread(out->text, 1, (size_t)out->text_len, f) != (size_t)out->text_len) { fclose(f); return 0; } }

    out->rdata_len = (int)sqo_r_u32(f);
    out->rdata = (uint8_t *)malloc((size_t)out->rdata_len + 1);
    if (out->rdata_len) { if (fread(out->rdata, 1, (size_t)out->rdata_len, f) != (size_t)out->rdata_len) { fclose(f); return 0; } }

    out->wdata_pool_size = (int)sqo_r_u32(f);

    out->reloc_count = (int)sqo_r_u32(f);
    out->relocs = (Relocation *)calloc((size_t)out->reloc_count, sizeof(Relocation));
    for (i = 0; i < out->reloc_count; i++) {
        out->relocs[i].offset = sqo_r_i32(f);
        out->relocs[i].kind   = (RelocKind)sqo_r_u32(f);
        out->relocs[i].addend = sqo_r_i32(f);
        out->relocs[i].symbol = sqo_r_str(f);
    }

    out->str_count = (int)sqo_r_u32(f);
    out->str_labels  = (char **)malloc((size_t)out->str_count * sizeof(char *));
    out->str_offsets = (int *)malloc((size_t)out->str_count * sizeof(int));
    for (i = 0; i < out->str_count; i++) { out->str_labels[i] = sqo_r_str(f); out->str_offsets[i] = sqo_r_i32(f); }

    out->wdata_count = (int)sqo_r_u32(f);
    out->wdata_labels  = (char **)malloc((size_t)out->wdata_count * sizeof(char *));
    out->wdata_offsets = (int *)malloc((size_t)out->wdata_count * sizeof(int));
    for (i = 0; i < out->wdata_count; i++) { out->wdata_labels[i] = sqo_r_str(f); out->wdata_offsets[i] = sqo_r_i32(f); }

    out->export_count = (int)sqo_r_u32(f);
    out->export_names   = (char **)malloc((size_t)out->export_count * sizeof(char *));
    out->export_offsets = (int *)malloc((size_t)out->export_count * sizeof(int));
    for (i = 0; i < out->export_count; i++) { out->export_names[i] = sqo_r_str(f); out->export_offsets[i] = sqo_r_i32(f); }

    out->import_count = (int)sqo_r_u32(f);
    out->import_specs = (char **)malloc((size_t)out->import_count * sizeof(char *));
    for (i = 0; i < out->import_count; i++) out->import_specs[i] = sqo_r_str(f);

    fclose(f);
    return 1;
}

static void sqo_free_objfile(ObjFile *obj) {
    int i;
    if (!obj) return;
    free(obj->text); free(obj->rdata);
    for (i = 0; i < obj->reloc_count; i++) free(obj->relocs[i].symbol);
    free(obj->relocs);
    for (i = 0; i < obj->str_count; i++) free(obj->str_labels[i]);
    free(obj->str_labels); free(obj->str_offsets);
    for (i = 0; i < obj->wdata_count; i++) free(obj->wdata_labels[i]);
    free(obj->wdata_labels); free(obj->wdata_offsets);
    for (i = 0; i < obj->export_count; i++) free(obj->export_names[i]);
    free(obj->export_names); free(obj->export_offsets);
    for (i = 0; i < obj->import_count; i++) free(obj->import_specs[i]);
    free(obj->import_specs);
    memset(obj, 0, sizeof(*obj));
}

/* Plain "mmap(NULL, ...)" (no hint) lets the kernel place the mapping
 * ANYWHERE -- for an anonymous mapping that's typically somewhere near
 * where shared libraries load, which is NOT guaranteed to be anywhere
 * close to this (often PIE-randomized) process's own executable code.
 * That matters a great deal here: every RELOC_STATIC_REL32 patched into
 * the loaded .text is a 32-bit PC-RELATIVE call/jump (±2GB range) to a
 * real function living in THIS process's own image (a csharp_rt.h host
 * symbol) -- confirmed this session via a real crash: an unhinted mmap
 * landed >2GB away from the host binary's own code, so every call from
 * loaded code correctly computed a huge displacement, silently truncated
 * to 32 bits by the very same patch32() logic elf_builder.c itself uses
 * for real linked executables (where this is never an issue, since a
 * whole program's .text is one contiguous, non-randomized-relative-to-
 * itself region) -- landing execution at a near-NULL, unmapped, garbage
 * target and crashing on the very first external call.
 *
 * Fixed by requesting mmap with an explicit HINT address near `anchor`
 * (a real in-process code address, e.g. a host symbol's own address) and
 * verifying the kernel actually honored it closely enough to stay within
 * a safe margin of the ±2GB RIP-relative range (1.5GB, leaving headroom
 * for the loaded regions' own sizes and any other nearby mappings) --
 * retrying at different offsets from the anchor if the first attempt
 * landed too far away, matching the standard technique real JIT engines
 * use for exactly this problem (a hint is advisory, not a guarantee, so
 * this is a best-effort loop, not a single blind attempt). */
#define SQO_NEAR_MAX (1536 * 1024 * 1024) /* 1.5GB safety margin, see above */

static void *sqo_alloc_pages_near(int len, int prot, void *anchor) {
    static const long step = 64 * 1024 * 1024; /* 64MB */
    int attempt;
    if (len < 1) len = 1;
    for (attempt = 0; attempt < 24; attempt++) {
        long delta = (attempt / 2 + 1) * step * ((attempt % 2) ? -1 : 1);
        void *hint = (void *)((char *)anchor + delta);
        void *m = mmap(hint, (unsigned long)len, prot, SQO_MAP_PRIVATE | SQO_MAP_ANONYMOUS, -1, 0);
        if (m == (void *)-1) continue;
        {
            intptr_t dist = (intptr_t)m - (intptr_t)anchor;
            if (dist < 0) dist = -dist;
            if (dist < SQO_NEAR_MAX) return m;
        }
        munmap(m, (unsigned long)len); /* too far away to be usable -- release and retry elsewhere */
    }
    return 0;
}


static void *find_host_symbol(const SqoHostSymbol *syms, int n, const char *name) {
    int i;
    for (i = 0; i < n; i++) if (strcmp(syms[i].name, name) == 0) return syms[i].addr;
    return 0;
}

/* Little-endian 32-bit store, matching e_patch32()'s own convention in
 * elf_builder.c (the exact same patching this loader mirrors, just
 * targeting live mmap'd memory instead of an ELF file's byte buffer). */
static void patch32(unsigned char *at, int32_t v) {
    at[0] = (unsigned char)(v);
    at[1] = (unsigned char)(v >> 8);
    at[2] = (unsigned char)(v >> 16);
    at[3] = (unsigned char)(v >> 24);
}

int sqo_loader_load(const char *path, const SqoHostSymbol *host_syms, int n_host_syms, SqoLoaded *out) {
    int i;
    memset(out, 0, sizeof(*out));

    if (!sqo_read_objfile(path, &out->obj)) {
        fprintf(stderr, "sqo_loader: could not read '%s'\n", path);
        return 0;
    }
    if (!(out->obj.is_64bit == 1 && out->obj.is_linux == 1 && out->obj.is_arm64 == 0)) {
        fprintf(stderr, "sqo_loader: '%s' was not compiled for -linux -64 (the only target this loader supports)\n", path);
        sqo_free_objfile(&out->obj);
        return 0;
    }

    out->text_len = out->obj.text_len;
    out->rdata_len = out->obj.rdata_len;
    out->wdata_len = out->obj.wdata_pool_size;

    /* All three regions start out READ|WRITE so relocations can be
     * patched in; .text is tightened to READ|EXEC (no longer writable)
     * once patching is done, and .rdata to READ-only -- .wdata stays
     * READ|WRITE throughout, since C# static/global fields are mutable
     * by definition.
     *
     * .text is placed NEAR a real host symbol's address (see
     * sqo_alloc_pages_near()'s own comment for why this is required, not
     * an optimization) -- falls back to this very function's own address
     * if the caller passed no host symbols at all (a script with no
     * external calls at all is a real, if unusual, case). .rdata/.wdata
     * are then placed near .text itself, so every relocation site (all
     * of them living in .text) stays within RIP-relative range of
     * whichever of the three regions it points at. */
    {
        /* `(void *)sqo_loader_load`, NOT `&sqo_loader_load` -- see
         * SQW/sqo_host_syms.c's own comment on why: `&function_name`
         * reads back NULL under squash's codegen, a real bug found this
         * session (the bare, auto-decaying function name works fine). */
        void *anchor = n_host_syms > 0 ? host_syms[0].addr : (void *)sqo_loader_load;
        out->text_mem = sqo_alloc_pages_near(out->text_len, SQO_PROT_READ | SQO_PROT_WRITE, anchor);
        out->rdata_mem = out->text_mem ? sqo_alloc_pages_near(out->rdata_len, SQO_PROT_READ | SQO_PROT_WRITE, out->text_mem) : 0;
        out->wdata_mem = out->text_mem ? sqo_alloc_pages_near(out->wdata_len, SQO_PROT_READ | SQO_PROT_WRITE, out->text_mem) : 0;
    }
    if (!out->text_mem || !out->rdata_mem || !out->wdata_mem) {
        fprintf(stderr, "sqo_loader: mmap (near a usable address) failed loading '%s'\n", path);
        sqo_loader_free(out);
        return 0;
    }
    if (out->obj.text_len) memcpy(out->text_mem, out->obj.text, (unsigned int)out->obj.text_len);
    if (out->obj.rdata_len) memcpy(out->rdata_mem, out->obj.rdata, (unsigned int)out->obj.rdata_len);
    /* wdata itself is never serialized (objfile.h: always zero-initialized
     * at load, matching every other squash link path) -- mmap's own
     * MAP_ANONYMOUS pages already come back zeroed. */

    for (i = 0; i < out->obj.reloc_count; i++) {
        Relocation *r = &out->obj.relocs[i];
        unsigned char *patch_at = (unsigned char *)out->text_mem + r->offset;
        void *target = 0;

        if (r->kind == RELOC_DATA_REL32) {
            int si;
            for (si = 0; si < out->obj.str_count; si++)
                if (strcmp(out->obj.str_labels[si], r->symbol) == 0) { target = (unsigned char *)out->rdata_mem + out->obj.str_offsets[si]; break; }
            if (!target) { fprintf(stderr, "sqo_loader: unresolved data symbol '%s' in '%s'\n", r->symbol, path); sqo_loader_free(out); return 0; }
        } else if (r->kind == RELOC_WDATA_REL32) {
            int wi;
            for (wi = 0; wi < out->obj.wdata_count; wi++)
                if (out->obj.wdata_labels[wi] && strcmp(out->obj.wdata_labels[wi], r->symbol) == 0) { target = (unsigned char *)out->wdata_mem + out->obj.wdata_offsets[wi]; break; }
            if (!target) { fprintf(stderr, "sqo_loader: unresolved wdata symbol '%s' in '%s'\n", r->symbol, path); sqo_loader_free(out); return 0; }
        } else if (r->kind == RELOC_STATIC_REL32) {
            int ei;
            for (ei = 0; ei < out->obj.export_count; ei++)
                if (strcmp(out->obj.export_names[ei], r->symbol) == 0) { target = (unsigned char *)out->text_mem + out->obj.export_offsets[ei]; break; }
            if (!target) target = find_host_symbol(host_syms, n_host_syms, r->symbol);
            if (!target) { fprintf(stderr, "sqo_loader: unresolved function symbol '%s' in '%s' (not in the object's own exports or the host symbol table)\n", r->symbol, path); sqo_loader_free(out); return 0; }
        } else if (r->symbol && strcmp(r->symbol, "__entry__") == 0) {
            /* A plain "-c" precompile with no linker attached never gets an
             * entry-point relocation injected (see compiler.c's own
             * inject_entry_reloc() -- only applies when !compile_only) --
             * kept as a defensive skip, not expected in practice here. */
            continue;
        } else {
            fprintf(stderr, "sqo_loader: unsupported relocation kind %d (symbol '%s') in '%s' -- only Linux x86-64 DATA/WDATA/STATIC_REL32 are supported\n",
                    (int)r->kind, r->symbol ? r->symbol : "?", path);
            sqo_loader_free(out);
            return 0;
        }

        {
            int64_t disp = (int64_t)((intptr_t)target - ((intptr_t)patch_at + 4));
            patch32(patch_at, (int32_t)disp);
        }
    }

    if (mprotect(out->text_mem, (unsigned long)(out->text_len < 1 ? 1 : out->text_len), SQO_PROT_READ | SQO_PROT_EXEC) != 0) {
        fprintf(stderr, "sqo_loader: mprotect(RX) failed for '%s'\n", path);
        sqo_loader_free(out);
        return 0;
    }
    mprotect(out->rdata_mem, (unsigned long)(out->rdata_len < 1 ? 1 : out->rdata_len), SQO_PROT_READ);
    return 1;
}

void *sqo_loader_get_symbol(SqoLoaded *m, const char *name) {
    int i;
    for (i = 0; i < m->obj.export_count; i++)
        if (strcmp(m->obj.export_names[i], name) == 0) return (unsigned char *)m->text_mem + m->obj.export_offsets[i];
    return 0;
}

void sqo_loader_free(SqoLoaded *m) {
    if (m->text_mem) munmap(m->text_mem, (unsigned long)(m->text_len < 1 ? 1 : m->text_len));
    if (m->rdata_mem) munmap(m->rdata_mem, (unsigned long)(m->rdata_len < 1 ? 1 : m->rdata_len));
    if (m->wdata_mem) munmap(m->wdata_mem, (unsigned long)(m->wdata_len < 1 ? 1 : m->wdata_len));
    sqo_free_objfile(&m->obj);
    memset(m, 0, sizeof(*m));
}
