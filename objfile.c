#include "objfile.h"
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* portable strdup replacement (matches the pattern used elsewhere in this
 * codebase — see ast.c's my_strdup) */
char *my_strdup(const char *s);

#define SQO_MAGIC "SQO1"

/* ---- little helpers for a simple length-prefixed binary format ---- */
static void w_u32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void w_i32(FILE *f, int32_t v)  { fwrite(&v, 4, 1, f); }
static void w_str(FILE *f, const char *s) {
    uint32_t len = s ? (uint32_t)strlen(s) : 0;
    w_u32(f, len);
    if (len) fwrite(s, 1, len, f);
}
static uint32_t r_u32(FILE *f) { uint32_t v=0; if (fread(&v,4,1,f)!=1) return 0; return v; }
static int32_t  r_i32(FILE *f) { int32_t v=0; if (fread(&v,4,1,f)!=1) return 0; return v; }
static char *r_str(FILE *f) {
    uint32_t len = r_u32(f);
    char *s = malloc(len+1);
    if (len) { if (fread(s,1,len,f)!=len) { s[0]=0; return s; } }
    s[len] = 0;
    return s;
}

int objfile_write(const char *path,
    int is_64bit, int is_linux, int is_arm64,
    const uint8_t *text, int text_len,
    const uint8_t *rdata, int rdata_len,
    int wdata_pool_size,
    const Relocation *relocs, int reloc_count,
    char **str_labels, int *str_offsets, int str_count,
    char **wdata_labels, int *wdata_offsets, int wdata_count,
    char **export_names, int *export_offsets, int export_count,
    char **import_specs, int import_count)
{
    FILE *f = fopen(path, "wb");
    if (!f) { diag_emit(DIAG_ERROR, -1, NULL, NULL, "cannot create object file '%s'", path); return 0; }

    fwrite(SQO_MAGIC, 1, 4, f);
    w_u32(f, (uint32_t)is_64bit);
    w_u32(f, (uint32_t)is_linux);
    w_u32(f, (uint32_t)is_arm64);

    w_u32(f, (uint32_t)text_len);
    if (text_len) fwrite(text, 1, (size_t)text_len, f);

    w_u32(f, (uint32_t)rdata_len);
    if (rdata_len) fwrite(rdata, 1, (size_t)rdata_len, f);

    w_u32(f, (uint32_t)wdata_pool_size);

    w_u32(f, (uint32_t)reloc_count);
    for (int i=0;i<reloc_count;i++) {
        w_i32(f, relocs[i].offset);
        w_u32(f, (uint32_t)relocs[i].kind);
        w_i32(f, relocs[i].addend);
        w_str(f, relocs[i].symbol);
    }

    w_u32(f, (uint32_t)str_count);
    for (int i=0;i<str_count;i++) { w_str(f, str_labels[i]); w_i32(f, str_offsets[i]); }

    w_u32(f, (uint32_t)wdata_count);
    for (int i=0;i<wdata_count;i++) { w_str(f, wdata_labels[i]); w_i32(f, wdata_offsets[i]); }

    w_u32(f, (uint32_t)export_count);
    for (int i=0;i<export_count;i++) { w_str(f, export_names[i]); w_i32(f, export_offsets[i]); }

    w_u32(f, (uint32_t)import_count);
    for (int i=0;i<import_count;i++) w_str(f, import_specs[i]);

    fclose(f);
    return 1;
}

int objfile_read(const char *path, ObjFile *out) {
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) { diag_emit(DIAG_ERROR, -1, NULL, NULL, "cannot open object file '%s'", path); return 0; }

    char magic[4];
    if (fread(magic,1,4,f)!=4 || memcmp(magic,SQO_MAGIC,4)!=0) {
        printf("objfile: '%s' is not a valid squash object file\n", path);
        fclose(f); return 0;
    }
    out->is_64bit = (int)r_u32(f);
    out->is_linux = (int)r_u32(f);
    out->is_arm64 = (int)r_u32(f);

    out->text_len = (int)r_u32(f);
    out->text = malloc((size_t)out->text_len + 1);
    if (out->text_len) { if (fread(out->text,1,(size_t)out->text_len,f) != (size_t)out->text_len) { fclose(f); return 0; } }

    out->rdata_len = (int)r_u32(f);
    out->rdata = malloc((size_t)out->rdata_len + 1);
    if (out->rdata_len) { if (fread(out->rdata,1,(size_t)out->rdata_len,f) != (size_t)out->rdata_len) { fclose(f); return 0; } }

    out->wdata_pool_size = (int)r_u32(f);

    out->reloc_count = (int)r_u32(f);
    out->relocs = calloc((size_t)out->reloc_count, sizeof(Relocation));
    for (int i=0;i<out->reloc_count;i++) {
        out->relocs[i].offset = r_i32(f);
        out->relocs[i].kind   = (RelocKind)r_u32(f);
        out->relocs[i].addend = r_i32(f);
        out->relocs[i].symbol = r_str(f);
    }

    out->str_count = (int)r_u32(f);
    out->str_labels  = malloc((size_t)out->str_count * sizeof(char*));
    out->str_offsets = malloc((size_t)out->str_count * sizeof(int));
    for (int i=0;i<out->str_count;i++) { out->str_labels[i]=r_str(f); out->str_offsets[i]=r_i32(f); }

    out->wdata_count = (int)r_u32(f);
    out->wdata_labels  = malloc((size_t)out->wdata_count * sizeof(char*));
    out->wdata_offsets = malloc((size_t)out->wdata_count * sizeof(int));
    for (int i=0;i<out->wdata_count;i++) { out->wdata_labels[i]=r_str(f); out->wdata_offsets[i]=r_i32(f); }

    out->export_count = (int)r_u32(f);
    out->export_names   = malloc((size_t)out->export_count * sizeof(char*));
    out->export_offsets = malloc((size_t)out->export_count * sizeof(int));
    for (int i=0;i<out->export_count;i++) { out->export_names[i]=r_str(f); out->export_offsets[i]=r_i32(f); }

    out->import_count = (int)r_u32(f);
    out->import_specs = malloc((size_t)out->import_count * sizeof(char*));
    for (int i=0;i<out->import_count;i++) out->import_specs[i]=r_str(f);

    fclose(f);
    return 1;
}

int objfile_target_matches(const ObjFile *obj, int is_64bit, int is_linux, int is_arm64) {
    return obj->is_64bit == is_64bit && obj->is_linux == is_linux && obj->is_arm64 == is_arm64;
}

void objfile_free(ObjFile *obj) {
    if (!obj) return;
    free(obj->text); free(obj->rdata);
    for (int i=0;i<obj->reloc_count;i++) free(obj->relocs[i].symbol);
    free(obj->relocs);
    for (int i=0;i<obj->str_count;i++) free(obj->str_labels[i]);
    free(obj->str_labels); free(obj->str_offsets);
    for (int i=0;i<obj->wdata_count;i++) free(obj->wdata_labels[i]);
    free(obj->wdata_labels); free(obj->wdata_offsets);
    for (int i=0;i<obj->export_count;i++) free(obj->export_names[i]);
    free(obj->export_names); free(obj->export_offsets);
    for (int i=0;i<obj->import_count;i++) free(obj->import_specs[i]);
    free(obj->import_specs);
    memset(obj, 0, sizeof(*obj));
}

/* ------------------------------------------------------------------------
 * objfile_merge — see objfile.h's own header comment for the overall
 * design. Summary of what happens here:
 *
 * 1. Concatenate every part's .text into one buffer (main first, then each
 *    object in order), recording each part's base offset.
 * 2. Same for .rdata (string/float-constant pool) and the wdata pool SIZE
 *    (wdata itself is always zero-initialized at load, so there's no bytes
 *    to concatenate, only offsets to rebase).
 * 3. Build ONE merged function-export table: (name -> absolute offset in
 *    the combined text) from main's own exports plus every object's
 *    exports, offsets rebased by that part's text base.
 * 4. Walk every relocation from every part:
 *      - RELOC_STATIC_REL32 (a cross-object function call — see codegen.c's
 *        AST_CALL comment for where these get emitted): look up the target
 *        in the merged export table. If found, patch the call's PC-relative
 *        displacement DIRECTLY into the combined text right now and DROP
 *        the relocation — the reference is fully resolved, nothing left
 *        for pe_link_and_write to do. If not found (genuinely undefined
 *        anywhere in this link), keep it in the output relocation list
 *        unresolved, same soft-failure behavior as every other kind.
 *      - RELOC_DATA_REL32/RELOC_DATA_ABS32 (string/float pool references)
 *        and RELOC_WDATA_REL32/RELOC_WDATA_ABS32 (globals/static-locals):
 *        rebase the relocation's own offset (it moved along with its
 *        part's .text), and rewrite its symbol name to the NAMESPACED
 *        label (see below) so it still finds its (also namespaced, also
 *        rebased) target in the merged string/wdata label tables.
 *      - Everything else (DLL imports, the synthetic "__entry__" marker):
 *        rebase the offset only: the symbol name is either a real,
 *        globally-meaningful DLL function name (imports) or handled
 *        specially by the caller (entry point — see compiler.c).
 *
 * Namespacing rationale: codegen's own string/float-constant labels
 * ("str0", "str1", …) and many wdata labels (static locals get mangled as
 * "static_<name>_<counter>", where <counter> is a PER-FILE counter) are
 * only guaranteed unique WITHIN one compile — two independently compiled
 * objects each start counting from "str0" again. Merging them verbatim
 * would silently make one object's relocation resolve to a DIFFERENT
 * object's same-named-by-coincidence string/wdata slot. Every object's
 * (0-based index i, main = -1) string/wdata labels are therefore prefixed
 * with "o<i>$" during merge, and every DATA relocation's symbol is rewritten
 * to match — but function EXPORT names are never touched, since those are
 * real C identifiers that must match by their literal name for cross-object
 * calls to mean anything.
 * ------------------------------------------------------------------------ */

typedef struct { char *name; int abs_offset; } ExportEnt;

/* Cross-object GLOBAL VARIABLE resolution -----------------------------------
 * A plain "extern int x;" forward declaration with no local definition still
 * gets its own (zero-initialized) wdata slot allocated in whichever TU
 * merely references it — squash's parser only special-cases this for
 * FUNCTIONS (a bodyless "extern" declaration is registered as a
 * SYM_IMPORT/RELOC_STATIC_REL32-resolved reference instead of allocating
 * storage; see parser_new4.c's ParseFunction), not variables. So the SAME
 * real global name can legitimately show up as a wdata label in more than
 * one part (main and/or several objects) — each one gets its own,
 * disconnected slot, and code in one part writing the global is invisible
 * to another part reading "the same" extern.
 *
 * Fixed here, at merge time, without touching codegen.c's parameter/global
 * allocation logic at all (see [[project-squash-sdl3-build]] for why that
 * felt too risky to touch directly): every wdata label that "looks like" a
 * real global variable (i.e. NOT one of codegen's own internal/mangled
 * names — static-locals are mangled "static_<name>_<counter>" and
 * compiler-internal slots are "__"-prefixed, both already correctly kept
 * separate per part) gets redirected, in EVERY part's relocations, to
 * whichever part defined a same-named wdata slot FIRST (main, then objects
 * in argument order) — so every reference to that name ends up pointing at
 * one shared slot instead of N disconnected ones. Each duplicate slot is
 * still allocated (wasting a few bytes) but never referenced once
 * redirected, which is simpler and safer than trying to remove it and
 * renumber every other offset. */
static int is_global_var_wdata_name(const char *label) {
    if (!label || !label[0]) return 0;
    if (strncmp(label, "static_", 7) == 0) return 0;
    if (strncmp(label, "__", 2) == 0) return 0;
    return 1;
}

typedef struct { const char *name; int part; } CanonEnt;

static int canon_find(CanonEnt *canon, int n, const char *name) {
    for (int i=0;i<n;i++) if (strcmp(canon[i].name, name)==0) return canon[i].part;
    return -2; /* sentinel: no wdata slot anywhere is named this */
}

static char *namespaced(int obj_index, const char *label) {
    /* obj_index == -1 means "the main compile" — left unprefixed since it's
     * always exactly one part and never collides with itself. */
    if (obj_index < 0) return my_strdup(label);
    char buf[300];
    snprintf(buf, sizeof buf, "o%d$%s", obj_index, label ? label : "");
    return my_strdup(buf);
}

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
    MergedOutput *out)
{
    memset(out, 0, sizeof(*out));

    /* ---- Step 1: compute base offsets and total sizes ---- */
    int *text_base  = malloc((size_t)(obj_count+1) * sizeof(int));
    int *rdata_base = malloc((size_t)(obj_count+1) * sizeof(int));
    int *wdata_base = malloc((size_t)(obj_count+1) * sizeof(int));

    text_base[0]  = 0;
    rdata_base[0] = 0;
    wdata_base[0] = 0;
    int total_text  = main_text_len;
    int total_rdata = main_rdata_len;
    int total_wdata = main_wdata_pool_size;
    for (int i=0;i<obj_count;i++) {
        text_base[i+1]  = total_text;
        rdata_base[i+1] = total_rdata;
        wdata_base[i+1] = total_wdata;
        total_text  += objs[i].text_len;
        total_rdata += objs[i].rdata_len;
        total_wdata += objs[i].wdata_pool_size;
    }

    out->text = malloc((size_t)total_text + 1);
    memcpy(out->text, main_text, (size_t)main_text_len);
    for (int i=0;i<obj_count;i++)
        memcpy(out->text + text_base[i+1], objs[i].text, (size_t)objs[i].text_len);
    out->text_len = total_text;

    out->rdata = malloc((size_t)total_rdata + 1);
    memcpy(out->rdata, main_rdata, (size_t)main_rdata_len);
    for (int i=0;i<obj_count;i++)
        memcpy(out->rdata + rdata_base[i+1], objs[i].rdata, (size_t)objs[i].rdata_len);
    out->rdata_len = total_rdata;

    out->wdata_pool_size = total_wdata;

    /* ---- Step 2: merged function export table ---- */
    int total_exports = main_export_count;
    for (int i=0;i<obj_count;i++) total_exports += objs[i].export_count;
    ExportEnt *exports = malloc((size_t)(total_exports>0?total_exports:1) * sizeof(ExportEnt));
    int ne = 0;
    int *static_init_offsets = malloc((size_t)(total_exports>0?total_exports:1) * sizeof(int));
    int static_init_count = 0;
    for (int k=0;k<main_export_count;k++) {
        exports[ne].name = main_export_names[k];
        exports[ne].abs_offset = text_base[0] + main_export_offsets[k];
        if (exports[ne].name && strcmp(exports[ne].name, "__sqx_static_init")==0)
            static_init_offsets[static_init_count++] = exports[ne].abs_offset;
        ne++;
    }
    for (int i=0;i<obj_count;i++) {
        for (int k=0;k<objs[i].export_count;k++) {
            exports[ne].name = objs[i].export_names[k];
            exports[ne].abs_offset = text_base[i+1] + objs[i].export_offsets[k];
            if (exports[ne].name && strcmp(exports[ne].name, "__sqx_static_init")==0)
                static_init_offsets[static_init_count++] = exports[ne].abs_offset;
            ne++;
        }
    }

    /* ---- Step 3: merged (namespaced) string/wdata label tables ---- */
    int total_str = main_str_count;
    for (int i=0;i<obj_count;i++) total_str += objs[i].str_count;
    out->str_labels  = malloc((size_t)(total_str>0?total_str:1) * sizeof(char*));
    out->str_offsets = malloc((size_t)(total_str>0?total_str:1) * sizeof(int));
    int ns = 0;
    for (int k=0;k<main_str_count;k++) {
        out->str_labels[ns]  = namespaced(-1, main_str_labels[k]);
        out->str_offsets[ns] = rdata_base[0] + main_str_offsets[k];
        ns++;
    }
    for (int i=0;i<obj_count;i++) {
        for (int k=0;k<objs[i].str_count;k++) {
            out->str_labels[ns]  = namespaced(i, objs[i].str_labels[k]);
            out->str_offsets[ns] = rdata_base[i+1] + objs[i].str_offsets[k];
            ns++;
        }
    }
    out->str_count = ns;

    int total_wd = main_wdata_count;
    for (int i=0;i<obj_count;i++) total_wd += objs[i].wdata_count;
    out->wdata_labels  = malloc((size_t)(total_wd>0?total_wd:1) * sizeof(char*));
    out->wdata_offsets = malloc((size_t)(total_wd>0?total_wd:1) * sizeof(int));
    int nw = 0;
    for (int k=0;k<main_wdata_count;k++) {
        out->wdata_labels[nw]  = namespaced(-1, main_wdata_labels[k]);
        out->wdata_offsets[nw] = wdata_base[0] + main_wdata_offsets[k];
        nw++;
    }
    for (int i=0;i<obj_count;i++) {
        for (int k=0;k<objs[i].wdata_count;k++) {
            out->wdata_labels[nw]  = namespaced(i, objs[i].wdata_labels[k]);
            out->wdata_offsets[nw] = wdata_base[i+1] + objs[i].wdata_offsets[k];
            nw++;
        }
    }
    out->wdata_count = nw;

    /* ---- Step 3b: canonical-part map for cross-object global variables
     * (see is_global_var_wdata_name()'s own comment above) ---- */
    int total_wd_names = main_wdata_count;
    for (int i=0;i<obj_count;i++) total_wd_names += objs[i].wdata_count;
    CanonEnt *canon = malloc((size_t)(total_wd_names>0?total_wd_names:1) * sizeof(CanonEnt));
    int nc = 0;
    for (int k=0;k<main_wdata_count;k++) {
        if (!is_global_var_wdata_name(main_wdata_labels[k])) continue;
        if (canon_find(canon, nc, main_wdata_labels[k]) == -2) {
            canon[nc].name = main_wdata_labels[k]; canon[nc].part = -1; nc++;
        }
    }
    for (int i=0;i<obj_count;i++) {
        for (int k=0;k<objs[i].wdata_count;k++) {
            if (!is_global_var_wdata_name(objs[i].wdata_labels[k])) continue;
            if (canon_find(canon, nc, objs[i].wdata_labels[k]) == -2) {
                canon[nc].name = objs[i].wdata_labels[k]; canon[nc].part = i; nc++;
            }
        }
    }

    /* ---- Step 4: merge + resolve relocations ---- */
    int total_relocs = main_reloc_count;
    for (int i=0;i<obj_count;i++) total_relocs += objs[i].reloc_count;
    Relocation *merged_relocs = malloc((size_t)(total_relocs>0?total_relocs:1) * sizeof(Relocation));
    int nr = 0;

    /* part_idx: -1 = main, else index into objs[] */
    for (int part = -1; part < obj_count; part++) {
        const Relocation *src_relocs = (part < 0) ? main_relocs : objs[part].relocs;
        int src_count = (part < 0) ? main_reloc_count : objs[part].reloc_count;
        int base = (part < 0) ? text_base[0] : text_base[part+1];

        for (int k=0;k<src_count;k++) {
            Relocation r = src_relocs[k];
            r.offset += base;

            if (r.symbol && strcmp(r.symbol,"__entry__")==0) {
                /* Only the main part's entry marker matters (see
                 * compiler.c) — carry it through unchanged (offset already
                 * rebased above, which is a no-op for part==-1 since
                 * base==0 there anyway). */
                merged_relocs[nr] = r;
                merged_relocs[nr].symbol = my_strdup(r.symbol);
                nr++;
                continue;
            }

            if (r.kind == RELOC_STATIC_REL32 || r.kind == RELOC_A64_STATIC_BL) {
                int found = 0;
                for (int e=0;e<ne;e++) {
                    if (r.symbol && strcmp(exports[e].name, r.symbol)==0) {
                        if (r.kind == RELOC_STATIC_REL32) {
                            int32_t disp = (int32_t)(exports[e].abs_offset - (r.offset + 4));
                            out->text[r.offset+0] = (uint8_t)(disp);
                            out->text[r.offset+1] = (uint8_t)(disp>>8);
                            out->text[r.offset+2] = (uint8_t)(disp>>16);
                            out->text[r.offset+3] = (uint8_t)(disp>>24);
                        } else {
                            /* RELOC_A64_STATIC_BL: pack a 26-bit word-offset
                             * signed immediate into the BL instruction's low
                             * 26 bits, preserving its top 6 opcode bits —
                             * same bit-packing as elf_builder.c's
                             * e_patch_a64_call26(), duplicated here (not
                             * shared) since objfile.c and elf_builder.c
                             * aren't otherwise coupled. */
                            uint32_t word = (uint32_t)out->text[r.offset+0]
                                          | ((uint32_t)out->text[r.offset+1]<<8)
                                          | ((uint32_t)out->text[r.offset+2]<<16)
                                          | ((uint32_t)out->text[r.offset+3]<<24);
                            int32_t word_disp = (exports[e].abs_offset - r.offset) / 4;
                            uint32_t imm26 = (uint32_t)word_disp & 0x03FFFFFFu;
                            uint32_t patched = (word & 0xFC000000u) | imm26;
                            out->text[r.offset+0] = (uint8_t)(patched);
                            out->text[r.offset+1] = (uint8_t)(patched>>8);
                            out->text[r.offset+2] = (uint8_t)(patched>>16);
                            out->text[r.offset+3] = (uint8_t)(patched>>24);
                        }
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    /* Genuinely undefined anywhere in this link — keep it
                     * so downstream tooling can at least warn, same as any
                     * other unresolved symbol. */
                    merged_relocs[nr] = r;
                    merged_relocs[nr].symbol = my_strdup(r.symbol);
                    nr++;
                }
                /* r.symbol is a BORROWED pointer (copied from
                 * src_relocs[k].symbol, still owned by main_relocs[]/
                 * objs[part].relocs[] and freed by the CALLER's own cleanup
                 * — asm_free()/objfile_free()) — must not be freed here. */
                continue;
            }

            if (r.kind == RELOC_DATA_REL32 || r.kind == RELOC_DATA_ABS32 ||
                r.kind == RELOC_WDATA_REL32 || r.kind == RELOC_WDATA_ABS32) {
                /* A real (non-static-local, non-internal) global variable
                 * name: redirect to whichever part's same-named slot is
                 * canonical, so every part's reference to "the same" extern
                 * global lands on one shared slot (see is_global_var_wdata_name()'s
                 * comment). Everything else (static-locals, "__"-internal
                 * slots) keeps resolving to its own part's slot exactly as
                 * before — same borrowed-pointer note as above applies. */
                int target_part = part;
                if (is_global_var_wdata_name(r.symbol)) {
                    int cp = canon_find(canon, nc, r.symbol);
                    if (cp != -2) target_part = cp;
                }
                r.symbol = namespaced(target_part, r.symbol);
                merged_relocs[nr] = r;
                nr++;
                continue;
            }

            /* Everything else (DLL imports via RELOC_IAT_REL32/RELOC_ABS32,
             * RELOC_TEXT_ABS32 function-pointer addresses, ARM64 kinds):
             * real, globally-meaningful names — carry through with only the
             * offset rebased. */
            merged_relocs[nr] = r;
            merged_relocs[nr].symbol = my_strdup(r.symbol);
            nr++;
        }
    }
    out->relocs = merged_relocs;
    out->reloc_count = nr;

    if (getenv("SQUASH_MERGE_DEBUG")) {
        int unresolved_static = 0;
        for (int i=0;i<nr;i++) if (merged_relocs[i].kind==RELOC_STATIC_REL32 || merged_relocs[i].kind==RELOC_A64_STATIC_BL) unresolved_static++;
        fprintf(stderr, "[merge-dbg] total input relocs merged/checked, output nr=%d, unresolved static-call relocs remaining=%d, total exports=%d\n", nr, unresolved_static, ne);
        for (int i=0;i<nr;i++) if (merged_relocs[i].kind==RELOC_STATIC_REL32 || merged_relocs[i].kind==RELOC_A64_STATIC_BL)
            fprintf(stderr, "[merge-dbg]   unresolved call to '%s' at merged text offset %d\n", merged_relocs[i].symbol, merged_relocs[i].offset);
    }

    /* ---- Step 5: merge DLL import specs (dedup by exact string) ---- */
    int total_imports = main_import_count;
    for (int i=0;i<obj_count;i++) total_imports += objs[i].import_count;
    char **merged_imports = malloc((size_t)(total_imports>0?total_imports:1) * sizeof(char*));
    int ni = 0;
    for (int k=0;k<main_import_count;k++) {
        int dup=0;
        for (int j=0;j<ni;j++) if (strcmp(merged_imports[j], main_import_specs[k])==0) { dup=1; break; }
        if (!dup) merged_imports[ni++] = my_strdup(main_import_specs[k]);
    }
    for (int i=0;i<obj_count;i++) {
        for (int k=0;k<objs[i].import_count;k++) {
            int dup=0;
            for (int j=0;j<ni;j++) if (strcmp(merged_imports[j], objs[i].import_specs[k])==0) { dup=1; break; }
            if (!dup) merged_imports[ni++] = my_strdup(objs[i].import_specs[k]);
        }
    }
    out->import_specs = merged_imports;
    out->import_count = ni;

    out->static_init_offsets = static_init_offsets;
    out->static_init_count = static_init_count;

    free(exports);
    free(canon);
    free(text_base); free(rdata_base); free(wdata_base);
}

void merged_output_free(MergedOutput *out) {
    if (!out) return;
    free(out->static_init_offsets);
    free(out->text); free(out->rdata);
    for (int i=0;i<out->reloc_count;i++) free(out->relocs[i].symbol);
    free(out->relocs);
    for (int i=0;i<out->str_count;i++) free(out->str_labels[i]);
    free(out->str_labels); free(out->str_offsets);
    for (int i=0;i<out->wdata_count;i++) free(out->wdata_labels[i]);
    free(out->wdata_labels); free(out->wdata_offsets);
    for (int i=0;i<out->import_count;i++) free(out->import_specs[i]);
    free(out->import_specs);
    memset(out, 0, sizeof(*out));
}
