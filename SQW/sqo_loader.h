#ifndef SQO_LOADER_H
#define SQO_LOADER_H
#include "../objfile.h"

/* =========================================================================
 * sqo_loader — a minimal, in-process ".sqo" loader (Phase 5's real
 * remaining design, per /home/squash/.claude/plans/nested-finding-walrus.md).
 *
 * Loads a squash object file (see objfile.h) into the CALLING process's
 * OWN memory (mmap'd pages, not a new process), resolves its relocations,
 * and hands back real, callable function pointers for its exported
 * functions — this is what lets a compiled C# `<script type="text/csharp">`
 * eventually be wired to real DOM events (click/input/etc.) in-process,
 * the way js_engine.c's own event dispatch works, instead of the
 * subprocess-per-script-run model sqw_run_csharp_script() (SQW/sqw_main.c)
 * currently uses for its "run once at page load" first slice.
 *
 * Scope of this first version, matching what a `.cs` script's own `-c`
 * compile output actually needs (verified by inspecting a real compiled
 * object's relocations this session): RELOC_STATIC_REL32 (calls to
 * functions not defined in this object — resolved against a caller-
 * supplied host symbol table, e.g. every CSR/csharp_rt.h function, since
 * a `.cs` script compiled with plain `-c` (no `.sqo` merging at compile
 * time — deliberately kept that way, see compiler.c's own `.cs` handling
 * comment on why merging isn't attempted mid-compile) still calls
 * csharp_rt functions by name, unresolved, exactly like any other
 * external call), RELOC_DATA_REL32 (string/float constants), and
 * RELOC_WDATA_REL32 (globals/statics). Linux x86-64 only (matches every
 * other target-specific integration in this project's SQW/SQS work so
 * far) — ARM64 relocation kinds and the Windows-only ABS32 kinds are
 * explicitly rejected with a clear diagnostic, not silently mishandled.
 *
 * The caller (SQW) is responsible for building the host symbol table —
 * see sqw_main.c's own registration of every csharp_rt.h export plus any
 * SQW-side DOM API functions a script is allowed to call. */

typedef struct { const char *name; void *addr; } SqoHostSymbol;

typedef struct {
    ObjFile obj;          /* parsed object; also used for export-name lookup */
    void   *text_mem;     /* mmap'd, PROT_READ|PROT_EXEC after loading */
    int     text_len;
    void   *rdata_mem;    /* mmap'd, PROT_READ after loading */
    int     rdata_len;
    void   *wdata_mem;    /* mmap'd, PROT_READ|PROT_WRITE (globals are mutable) */
    int     wdata_len;
} SqoLoaded;

/* Loads `path` (a ".sqo" compiled for -linux -64) into the current
 * process, resolving every relocation against `host_syms`
 * (n_host_syms entries) plus the object's own export table (for any
 * self-referential RELOC_STATIC_REL32, though ordinary same-object calls
 * are normally already direct-label calls with no relocation at all —
 * this is a robustness fallback, not the common case). Returns 1 on
 * success (out is filled in and must eventually be passed to
 * sqo_loader_free()), 0 on failure (a target mismatch, an unresolved
 * symbol, or an unsupported relocation kind — a clear diagnostic is
 * printed to stderr either way; out is left zeroed, safe to pass to
 * sqo_loader_free() as a no-op). */
int sqo_loader_load(const char *path, const SqoHostSymbol *host_syms, int n_host_syms, SqoLoaded *out);

/* Returns the runtime address of exported function `name` from an
 * already-loaded object, or NULL if it has no export by that name. The
 * caller casts this to the real function-pointer type it expects (this
 * loader has no type information to check against — same "the caller
 * knows the real signature" contract as dlsym()). */
void *sqo_loader_get_symbol(SqoLoaded *m, const char *name);

/* Unmaps everything and frees the parsed ObjFile. Safe to call on a
 * zeroed/failed-load SqoLoaded (a no-op in that case). */
void sqo_loader_free(SqoLoaded *m);

#endif /* SQO_LOADER_H */
