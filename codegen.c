#include "codegen.h"
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* portable strdup replacement */
char* my_strdup(const char* src);

/* forward declarations */
static void asm_movq_gpr_from_xmm(Assembler *a, int gpr, int xmm);
static int sizeof_type_sym(TypeInfo *ti, int is_64, SymTable *sym);
static int expr_has_call(ASTNode *n);
static int is_64bit_int_arg(CodeGen *cg, ASTNode *n);
static void push_64bit_int_arg(CodeGen *cg, ASTNode *n);
static int field_scalar_size(CodeGen *cg, ASTNode *obj, const char *field_name, int *out_is2d);
static TypeInfo *field_type_of(CodeGen *cg, ASTNode *obj, const char *field_name);
static int field_array_size_of(CodeGen *cg, ASTNode *obj, const char *field_name);
static int effective_typeinfo_size(CodeGen *cg, TypeInfo *t, int is_64bit);
static int elem_size_of(CodeGen *cg, ASTNode *arr_expr);
static int pointer_pointee_size(CodeGen *cg, ASTNode *node);
static int deref_struct_size(CodeGen *cg, ASTNode *operand);
static int var_struct_size(CodeGen *cg, ASTNode *var_node);
static int struct_copy_size_of(CodeGen *cg, ASTNode *node);
static void struct_copy_addr_of(CodeGen *cg, ASTNode *node);
static int float_store_target_size(CodeGen *cg, ASTNode *lhs);
static int lvalue_target_is_float(CodeGen *cg, ASTNode *lhs);
static int index_elem_is_float(CodeGen *cg, ASTNode *idxnode);
static int float_expr_width(CodeGen *cg, ASTNode *n);
static int param_is_single_float(Symbol *func_sym, int i, SymTable *st, int is_64);

/* Resolve a (possibly typedef'd) base type name down through the full
 * typedef chain to see if it ultimately means a byte/short-sized integer —
 * e.g. SDL3's "Uint8" -> "uint8_t" -> "unsigned char", or a typedef aliasing
 * a builtin directly ("typedef unsigned char Uint8;"), any chain depth. */
static int is_byte_sized_stdint(SymTable *sym, const char *base) {
    for (int hops=0; base && hops<8; hops++) {
        if (strcmp(base,"uint8_t")==0 || strcmp(base,"int8_t")==0 ||
            strcmp(base,"char")==0 || strcmp(base,"signed char")==0 ||
            strcmp(base,"unsigned char")==0) return 1;
        Symbol *td = symtable_lookup(sym, base);
        if (td && td->kind==SYM_TYPEDEF && td->type && td->type->pointer_depth==0 && td->type->base)
            base = td->type->base;
        else
            return 0;
    }
    return 0;
}
static int is_short_sized_stdint(SymTable *sym, const char *base) {
    for (int hops=0; base && hops<8; hops++) {
        if (strcmp(base,"uint16_t")==0 || strcmp(base,"int16_t")==0 ||
            strcmp(base,"short")==0 || strcmp(base,"unsigned short")==0) return 1;
        Symbol *td = symtable_lookup(sym, base);
        if (td && td->kind==SYM_TYPEDEF && td->type && td->type->pointer_depth==0 && td->type->base)
            base = td->type->base;
        else
            return 0;
    }
    return 0;
}

/* =========================================================================
 * resolve_node_type — given an expression node, return bare struct/union type name
 * Handles AST_VAR and nested AST_MEMBER chains recursively.
 * Returns pointer into stable AST/symtable memory (NOT a local buffer), or NULL.
 * ========================================================================= */
static const char *resolve_node_type(SymTable *sym, ASTNode *node) {
    if (!node) return NULL;
    if (node->kind == AST_DEREF) {
        /* "*ptr" used as the base of a further .field access (e.g.
         * "(*p).field", the explicit-dereference-then-dot spelling of
         * "p->field" — extremely common inside macros that take a value
         * "x" and are invoked as MACRO(*ptr), substituting "(*ptr).field"
         * everywhere the macro body writes "(x).field", e.g. SDL3's own
         * SDL_AUDIO_FRAMESIZE(x) used as SDL_AUDIO_FRAMESIZE(*spec)).
         * Dereferencing doesn't change the pointee's base type NAME (only
         * arrow/dot syntax and pointer_depth differ), so the operand's own
         * resolved type is directly the answer — same base name a plain
         * "ptr->field" (AST_VAR, arrow=1) access already resolves via the
         * pointer variable's own TypeInfo.base, which is pointer-depth-
         * agnostic. Without this case, EVERY "(*ptr).field" access fell
         * through every other kind check here and returned NULL, which
         * made field_byte_offset() (see its own now-matching AST_DEREF fix)
         * return 0 for every field — silently reading/writing the FIRST
         * field's memory regardless of which field was actually named. */
        return resolve_node_type(sym, node->deref.operand);
    }
    if (node->kind == AST_CAST && node->cast.type && node->cast.type->base) {
        /* (T*)expr / (T)expr used as the base of a further ->field/.field
         * chain (e.g. COM-style "((Foo*)p)->lpVtbl->Method()", where THIS
         * call resolves "((Foo*)p)->lpVtbl"'s own type so the OUTER
         * "->Method" access knows what struct it's in) — the cast already
         * names the type directly, no need to trace back through a
         * declared variable (which this function otherwise can't do
         * through an arbitrary cast, silently returning NULL). Without
         * this, any 2-or-more-level member chain rooted in a cast (single-
         * level chains are handled by the AST_MEMBER case's own direct
         * AST_CAST check elsewhere in this file) resolved the wrong
         * struct type for every level past the first, corrupting the
         * field offset/width for the OUTER access — confirmed via a
         * minimal repro: "((Foo*)p)->lpVtbl->GetValue()" (a vtable-style
         * double-arrow call through a cast) read GetValue's function
         * pointer with a truncated (32-bit) load and crashed calling
         * through the corrupted address. */
        const char *tn = node->cast.type->base;
        if (strncmp(tn,"struct ",7)==0) return tn+7;
        if (strncmp(tn,"union ",6)==0)  return tn+6;
        Symbol *td = symtable_lookup(sym, tn);
        if (td && td->kind == SYM_TYPEDEF && td->type && td->type->base) {
            const char *tb = td->type->base;
            if (strncmp(tb,"struct ",7)==0) return tb+7;
            if (strncmp(tb,"union ",6)==0)  return tb+6;
        }
        return NULL;
    }
    if (node->kind == AST_VAR) {
        Symbol *vs = symtable_lookup(sym, node->var.name);
        if (!vs || !vs->type || !vs->type->base) return NULL;
        const char *tn = vs->type->base;
        if (strncmp(tn,"struct ",7)==0) return tn+7;
        if (strncmp(tn,"union ",6)==0)  return tn+6;
        /* typedef: resolve one level */
        Symbol *td = symtable_lookup(sym, tn);
        if (td && td->kind == SYM_TYPEDEF && td->type && td->type->base) {
            const char *tb = td->type->base;
            if (strncmp(tb,"struct ",7)==0) return tb+7;
            if (strncmp(tb,"union ",6)==0)  return tb+6;
        }
        return NULL;
    }
    if (node->kind == AST_INDEX) {
        /* arr[i] — element type is the type of arr (with one pointer/array level stripped).
         * Handles arr[i]->field and arr[i].field chains. */
        ASTNode *arr = node->index.array;
        /* Resolve element type from the array expression */
        const char *arr_type = resolve_node_type(sym, arr);
        if (arr_type) return arr_type; /* already a struct type */
        /* Try: if arr is AST_VAR, look up its type and strip one pointer level */
        if (arr && arr->kind == AST_VAR) {
            Symbol *vs = symtable_lookup(sym, arr->var.name);
            if (vs && vs->type && vs->type->base) {
                const char *tn = vs->type->base;
                if (strncmp(tn,"struct ",7)==0) return tn+7;
                if (strncmp(tn,"union ",6)==0)  return tn+6;
                Symbol *td = symtable_lookup(sym, tn);
                if (td && td->kind == SYM_TYPEDEF && td->type && td->type->base) {
                    const char *tb = td->type->base;
                    if (strncmp(tb,"struct ",7)==0) return tb+7;
                    if (strncmp(tb,"union ",6)==0)  return tb+6;
                }
            }
        }
        /* arr is AST_MEMBER: resolve the member's type */
        if (arr && arr->kind == AST_MEMBER) {
            return resolve_node_type(sym, arr);
        }
        return NULL;
    }
    if (node->kind == AST_MEMBER) {
        /* Recursively resolve the parent expression's struct type */
        const char *parent_struct = resolve_node_type(sym, node->member.obj);
        if (!parent_struct) return NULL;
        /* Look up the parent struct/union definition. resolve_node_type's
         * return value is always the BARE tag name with no "struct "/
         * "union " prefix (see every return site above and below), so
         * there's no way to tell here whether the parent was originally a
         * struct or a union — try "struct " first, then fall back to
         * "union ". Without this fallback, any member-chain that passes
         * through an anonymous UNION field (e.g. SDL3's own
         * SDL_RenderCommand: "union { struct {...} color; ... } data;",
         * accessed as "cmd->data.color.color") silently failed to resolve
         * past the union hop — "struct $anonN" never matches a union
         * registered under "union $anonN" — which made every caller of
         * this function (struct_copy_size_of's whole-struct-assignment
         * fast path among them) treat the chain as "not a struct type",
         * silently falling back to a truncating scalar copy. */
        char pkey[256]; snprintf(pkey, sizeof pkey, "struct %s", parent_struct);
        Symbol *pss = symtable_lookup(sym, pkey);
        if (!pss || !pss->struct_node) {
            snprintf(pkey, sizeof pkey, "union %s", parent_struct);
            pss = symtable_lookup(sym, pkey);
        }
        if (!pss || !pss->struct_node) return NULL;
        /* Find the named field and return its type */
        const char *fname = node->member.field;
        ASTNode *psd = pss->struct_node;
        int fi;
        for (fi = 0; fi < psd->struct_decl.nfields; fi++) {
            ASTNode *ff = psd->struct_decl.fields[fi];
            if (!ff || ff->kind != AST_FIELD || !ff->field.name) continue;
            if (strcmp(ff->field.name, fname) != 0) continue;
            if (!ff->field.type || !ff->field.type->base) return NULL;
            const char *fbase = ff->field.type->base;
            if (strncmp(fbase,"struct ",7)==0) return fbase+7;
            if (strncmp(fbase,"union ",6)==0)  return fbase+6;
            /* typedef: resolve one level */
            Symbol *td = symtable_lookup(sym, fbase);
            if (td && td->kind == SYM_TYPEDEF && td->type && td->type->base) {
                const char *tb = td->type->base;
                if (strncmp(tb,"struct ",7)==0) return tb+7;
                if (strncmp(tb,"union ",6)==0)  return tb+6;
            }
            return NULL;
        }
        /* Not found at top level — search inside __anon_N fields (C anonymous unions/structs) */
        for (fi = 0; fi < psd->struct_decl.nfields; fi++) {
            ASTNode *ff = psd->struct_decl.fields[fi];
            if (!ff || ff->kind != AST_FIELD || !ff->field.name) continue;
            if (strncmp(ff->field.name, "__anon_", 7) != 0) continue;
            if (!ff->field.type || !ff->field.type->base) continue;
            const char *atype = ff->field.type->base;
            const char *bare = atype;
            /* Preserve the original prefix (struct/union) for the lookup key */
            const char *orig_prefix = "struct ";
            if (strncmp(bare,"struct ",7)==0) bare+=7;
            else if (strncmp(bare,"union ",6)==0) { bare+=6; orig_prefix="union "; }
            char akey[256]; snprintf(akey, sizeof akey, "%s%s", orig_prefix, bare);
            Symbol *asym = symtable_lookup(sym, akey);
            /* Also try "struct" prefix as fallback for cases where sym was stored that way */
            if (!asym || !asym->struct_node) {
                snprintf(akey, sizeof akey, "struct %s", bare);
                asym = symtable_lookup(sym, akey);
            }
            if (!asym || !asym->struct_node) continue;
            ASTNode *asd = asym->struct_node;
            int aj;
            for (aj = 0; aj < asd->struct_decl.nfields; aj++) {
                ASTNode *af = asd->struct_decl.fields[aj];
                if (!af || af->kind != AST_FIELD || !af->field.name) continue;
                if (strcmp(af->field.name, fname) != 0) continue;
                if (!af->field.type || !af->field.type->base) return NULL;
                const char *fbase2 = af->field.type->base;
                if (strncmp(fbase2,"struct ",7)==0) return fbase2+7;
                if (strncmp(fbase2,"union ",6)==0)  return fbase2+6;
                /* typedef: resolve one level */
                Symbol *td2 = symtable_lookup(sym, fbase2);
                if (td2 && td2->kind == SYM_TYPEDEF && td2->type && td2->type->base) {
                    const char *tb2 = td2->type->base;
                    if (strncmp(tb2,"struct ",7)==0) return tb2+7;
                    if (strncmp(tb2,"union ",6)==0)  return tb2+6;
                }
                return NULL;
            }
        }
        return NULL;
    }
    return NULL;
}

/* =========================================================================
 * field_byte_offset — walk struct/union field list and return byte offset
 * For unions every field is at offset 0.
 * Fields are stored as AST_FIELD nodes; each is sizeof(int)=4 bytes wide
 * (we assume int-sized fields throughout; a full compiler would use TypeInfo).
 * ========================================================================= */
static int field_byte_offset(SymTable *sym, ASTNode *obj_node, const char *field_name) {
    /* Find the struct/union type name from the object's expression type.
     * The simplest heuristic: if obj is AST_VAR, look up its declaration
     * and get the type name from its TypeInfo or from the struct_decl.     */
    const char *type_name = NULL;

    /* Walk up: obj_node is the struct variable or nested member.
     * Its symbol/type gives us the TypeInfo whose name is "struct Foo" or "Foo". */
    if (obj_node && obj_node->kind == AST_VAR) {
        Symbol *vs = symtable_lookup(sym, obj_node->var.name);
        if (vs && vs->type) {
            const char *tn = vs->type->base;
            if (tn) {
                if (strncmp(tn,"struct ",7)==0) tn+=7;
                else if (strncmp(tn,"union ",6)==0)  tn+=6;
                type_name = tn;
            }
        }
    } else if (obj_node && obj_node->kind == AST_INDEX) {
        /* Array subscript like arr[i] — resolve type of array elements.
         * arr[i].field: arr is either a local/param/global var or a struct member. */
        ASTNode *arr = obj_node->index.array;
        /* Find the element type of 'arr' */
        if (arr && arr->kind == AST_VAR) {
            Symbol *vs = symtable_lookup(sym, arr->var.name);
            if (getenv("SQUASH_FBO_DEBUG")) {
                fprintf(stderr, "[FBO] arr='%s' vs=%p vs->type=%p base='%s' ptrdepth=%d arrsz=%d field='%s'\n",
                        arr->var.name, (void*)vs,
                        vs?(void*)vs->type:NULL,
                        (vs&&vs->type&&vs->type->base)?vs->type->base:"(null)",
                        (vs&&vs->type)?vs->type->pointer_depth:-999,
                        (vs&&vs->type)?vs->type->array_size:-999,
                        field_name);
            }
            if (vs && vs->type) {
                /* Element type is the type with one less pointer/array level */
                const char *tn = vs->type->base;
                if (tn) {
                    /* If type has pointer_depth > 0, the element is what it points to */
                    /* For array-of-struct: base is the struct/typedef name */
                    if (strncmp(tn,"struct ",7)==0) tn+=7;
                    else if (strncmp(tn,"union ",6)==0)  tn+=6;
                    else {
                        /* Could be typedef → resolve */
                        Symbol *td = symtable_lookup(sym, tn);
                        if (getenv("SQUASH_FBO_DEBUG")) {
                            fprintf(stderr, "[FBO] typedef lookup '%s' -> td=%p kind=%d type=%p base='%s'\n",
                                    tn, (void*)td, td?td->kind:-1,
                                    (td&&td->type)?(void*)td->type:NULL,
                                    (td&&td->type&&td->type->base)?td->type->base:"(null)");
                        }
                        if (td && td->kind == SYM_TYPEDEF && td->type && td->type->base) {
                            const char *tb = td->type->base;
                            if (strncmp(tb,"struct ",7)==0) tb+=7;
                            else if (strncmp(tb,"union ",6)==0) tb+=6;
                            tn = tb;
                        }
                    }
                    type_name = tn;
                }
            }
            if (getenv("SQUASH_FBO_DEBUG")) {
                fprintf(stderr, "[FBO] resolved type_name='%s'\n", type_name?type_name:"(null)");
            }
        } else if (arr && arr->kind == AST_MEMBER) {
            /* struct_var.field[i].member — resolve field's element type */
            ASTNode *parent = arr->member.obj;
            const char *parent_type = NULL;
            if (parent && parent->kind == AST_VAR) {
                Symbol *pv = symtable_lookup(sym, parent->var.name);
                if (pv && pv->type) parent_type = pv->type->base;
            }
            if (!parent_type) {
                /* parent is a nested member chain — use resolve_node_type recursively */
                type_name = resolve_node_type(sym, arr);
            }
            if (parent_type) {
                const char *bare = parent_type;
                if (strncmp(bare,"struct ",7)==0) bare+=7;
                else if (strncmp(bare,"union ",6)==0) bare+=6;
                else {
                    Symbol *td = symtable_lookup(sym, parent_type);
                    if (td && td->kind == SYM_TYPEDEF && td->type) {
                        const char *tb = td->type->base;
                        if (strncmp(tb,"struct ",7)==0) tb+=7;
                        else if (strncmp(tb,"union ",6)==0) tb+=6;
                        bare = tb;
                        parent_type = bare;
                    }
                }
                char pkey[256]; snprintf(pkey,sizeof pkey,"struct %s",bare);
                Symbol *pss = symtable_lookup(sym, pkey);
                if (!pss || !pss->struct_node) {
                    /* bare may name a union — see the fallback in
                     * resolve_node_type()'s AST_MEMBER case for the full
                     * explanation. */
                    snprintf(pkey,sizeof pkey,"union %s",bare);
                    pss = symtable_lookup(sym, pkey);
                }
                if (pss && pss->struct_node) {
                    const char *fname2 = arr->member.field;
                    ASTNode *psd = pss->struct_node;
                    for (int fi=0; fi<psd->struct_decl.nfields; fi++) {
                        ASTNode *ff = psd->struct_decl.fields[fi];
                        if (ff && ff->kind==AST_FIELD && ff->field.name &&
                            strcmp(ff->field.name, fname2)==0 && ff->field.type) {
                            const char *fbase = ff->field.type->base;
                            if (fbase) {
                                if (strncmp(fbase,"struct ",7)==0) fbase+=7;
                                else if (strncmp(fbase,"union ",6)==0) fbase+=6;
                                else {
                                    Symbol *td2 = symtable_lookup(sym, fbase);
                                    if (td2 && td2->kind == SYM_TYPEDEF && td2->type && td2->type->base) {
                                        const char *tb2 = td2->type->base;
                                        if (strncmp(tb2,"struct ",7)==0) tb2+=7;
                                        else if (strncmp(tb2,"union ",6)==0) tb2+=6;
                                        fbase = tb2;
                                    }
                                }
                                type_name = fbase;
                            }
                            break;
                        }
                    }
                }
            }
        }
    } else if (obj_node && obj_node->kind == AST_MEMBER) {
        /* Use recursive helper to resolve nested member chains like p->lex->cur */
        type_name = resolve_node_type(sym, obj_node);
        if (!type_name) {
            /* field not found in chain */
        }
    } else if (obj_node && obj_node->kind == AST_DEREF) {
        /* "(*ptr).field" — see resolve_node_type()'s identical AST_DEREF
         * case for the full explanation (this is the OFFSET-computing twin
         * of that TYPE-resolving fix; without it this function fell through
         * to "if (!type_name) return 0" for every such access, so every
         * field of a dereferenced-then-dot-accessed struct silently
         * computed as byte offset 0 — the actual root cause of the SDL3
         * audio-stream hang: SDL_AUDIO_FRAMESIZE(*spec)'s "(x).channels"
         * read back "(x).format"'s value instead, producing a frame size
         * so large that "capacity -= capacity % framesize" zeroed the
         * new audio track's capacity, which made WriteToAudioTrack() never
         * write anything and SDL_WriteToAudioQueue() loop forever
         * allocating zero-capacity tracks). */
        type_name = resolve_node_type(sym, obj_node);
    } else if (obj_node && obj_node->kind == AST_CAST && obj_node->cast.type) {
        /* ((T*)expr)->field / ((T*)expr).field — an explicit cast names the
         * pointee type directly (same reasoning as the identical AST_CAST
         * case in codegen_expr's own AST_MEMBER field-width resolution —
         * see its comment for the real-world COM-vtable-call bug this
         * fixes; without this, every OFFSET (not just width) for a field
         * accessed through a cast base silently defaulted to 0). */
        const char *tn = obj_node->cast.type->base;
        if (tn) {
            if (strncmp(tn,"struct ",7)==0) tn+=7;
            else if (strncmp(tn,"union ",6)==0) tn+=6;
            type_name = tn;
        }
    }
    if (!type_name) return 0;

     /* Look up the struct symbol — symtable_define_struct stores key as "struct <name>" */
     char key[128];
     /* symtable_define_struct always stores with "struct " prefix */
     const char *bare_tn2 = type_name;
     if (strncmp(bare_tn2,"struct ",7)==0) bare_tn2+=7;
     else if (strncmp(bare_tn2,"union ",6)==0) bare_tn2+=6;
     snprintf(key,sizeof key,"struct %s",bare_tn2);
     Symbol *ss = symtable_lookup(sym, key);
     if (!ss || !ss->struct_node) {
         /* bare_tn2 may name a union, not a struct — see the identical
          * fallback (and full explanation) in resolve_node_type()'s
          * AST_MEMBER case near the top of this file. Without this,
          * field_byte_offset() (the function that computes the ACTUAL
          * runtime offset for a member read/write) returns 0 for any
          * member chain passing through an anonymous union field —
          * e.g. SDL3's own SDL_RenderCommand "cmd->data.color.color.r" —
          * silently reading/writing byte 0 of the struct instead of the
          * real field. */
         snprintf(key,sizeof key,"union %s",bare_tn2);
         ss = symtable_lookup(sym, key);
     }
     /* If not found, type_name might be a typedef (e.g. "Vec2" -> "struct anon").
      * Resolve the typedef chain to find the actual struct definition. */
     if (!ss || !ss->struct_node) {
         Symbol *tds = symtable_lookup(sym, type_name);
         if (tds && tds->kind == SYM_TYPEDEF && tds->type && tds->type->base) {
             const char *td_base = tds->type->base;
             const char *bare_td = td_base;
             if (strncmp(bare_td,"struct ",7)==0) bare_td+=7;
             else if (strncmp(bare_td,"union ",6)==0) bare_td+=6;
             char key2[128]; snprintf(key2,sizeof key2,"struct %s",bare_td);
             Symbol *ss2 = symtable_lookup(sym, key2);
             if (ss2 && ss2->struct_node) ss = ss2;
         }
     }
    if (!ss || !ss->struct_node) {
        return 0;
    }

    ASTNode *sd = ss->struct_node;   /* AST_STRUCT_DECL */
    int is_union = sd->struct_decl.is_union;

    /* Walk fields; compute correct size for each field type. */
    for (int i=0;i<sd->struct_decl.nfields;i++) { ASTNode *__f2=sd->struct_decl.fields[i];
 }
    int offset = 0;
    for (int i = 0; i < sd->struct_decl.nfields; i++) {
        ASTNode *f = sd->struct_decl.fields[i];
        if (!f || f->kind != AST_FIELD) continue;
        if (f->field.name && strcmp(f->field.name, field_name)==0) {
            if (is_union) return 0;
            /* Apply the target field's own alignment before returning */
            int tfsz = 4;
            int tstruct_align = 0; /* real alignment if this field is struct/union-typed; see symtable_compute_struct_alignment()'s comment for why this can't just be min(size,8) */
            if (f->field.type) {
                /* typeinfo_size() alone is typedef-blind (no SymTable access) —
                 * it only recognizes bare builtin/stdint names, so a field typed
                 * with a project typedef that ultimately means a narrow integer
                 * (e.g. SDL3's "Uint8"/"Uint16", typedef'd to unsigned char/short)
                 * fell through to its generic "4 (default for structs etc)"
                 * return. sizeof_type_sym() already does the typedef-chain walk
                 * needed here (used correctly elsewhere for whole-struct sizing);
                 * reusing it for this per-field size instead fixes every field
                 * offset computed AFTER a narrow typedef'd field in the same
                 * struct, which up to now silently rounded up to a 4-byte
                 * boundary too early — e.g. SDL_PixelFormatDetails's own
                 * "bytes_per_pixel" (Uint8) landed at offset 8 instead of 5,
                 * clobbering Rmask's first byte, and every subsequent Uint8
                 * field (Rbits/Gbits/Bbits/Abits, Rshift/Gshift/Bshift/Ashift)
                 * likewise misaligned — the exact corruption that caused
                 * SDL_InitPixelFormatDetails() to compute completely wrong
                 * shift/bit counts (masking video init hangs/misbehavior
                 * downstream in real SDL3 pixel-format-heavy code). */
                tfsz = sizeof_type_sym(f->field.type, sym->is_64bit, sym);
                /* Resolve typedef/struct for alignment */
                if (f->field.type->pointer_depth == 0 && f->field.type->base) {
                    const char *tb2 = f->field.type->base;
                    const char *tbare2 = tb2;
                    if (strncmp(tbare2,"struct ",7)==0) tbare2+=7;
                    else if (strncmp(tbare2,"union ",6)==0) tbare2+=6;
                    if (tbare2 != tb2) {
                        char sk2[256]; snprintf(sk2,sizeof sk2,"struct %s",tbare2);
                        Symbol *ts2 = symtable_lookup(sym, sk2);
                        if (ts2 && ts2->struct_size > 0) tfsz = ts2->struct_size;
                        if (ts2 && ts2->struct_node) tstruct_align = symtable_compute_struct_alignment(sym, ts2->struct_node);
                    } else {
                        Symbol *ttd = symtable_lookup(sym, tb2);
                        if (ttd && ttd->kind == SYM_TYPEDEF && ttd->type && ttd->type->pointer_depth > 0) {
                            /* Pointer typedef (e.g. "typedef struct HWND__ {...}
                             * *HWND;" via DECLARE_HANDLE, or "typedef void*
                             * HICON;"). MUST short-circuit here: falling through
                             * to the struct/union resolution below would strip
                             * the "struct " prefix off ttd->type->base (e.g.
                             * "struct HWND__") and look up THAT struct's own
                             * alignment (4, from its single dummy int member)
                             * instead of recognizing that the field itself is a
                             * POINTER needing 8-byte alignment regardless of
                             * what it points to. That bug placed every pointer-
                             * typedef'd field (any DECLARE_HANDLE-style HANDLE
                             * type — HWND, HICON, HMODULE, ...) 4 bytes too
                             * early whenever offsetof()/&(ptr->field) was used
                             * on it after an odd number of 4-byte fields, e.g.
                             * DXGI_SWAP_CHAIN_DESC's OutputWindow — corrupting
                             * the real ABI layout passed to actual system DLLs
                             * (confirmed via D3D11CreateDeviceAndSwapChain
                             * returning DXGI_ERROR_INVALID_CALL because
                             * OutputWindow landed at a byte offset the real
                             * dxgi.dll didn't expect). */
                            tfsz = sym->is_64bit ? 8 : 4;
                            tstruct_align = tfsz;
                        } else if (ttd && ttd->kind == SYM_TYPEDEF && ttd->type) {
                            /* Same multi-hop-typedef fix as below — plain
                             * typeinfo_size() only resolves one hop. Currently
                             * only used via "if (rs > tfsz) tfsz = rs;" below,
                             * so an under-resolved `rs` couldn't previously
                             * overwrite a correct larger tfsz, but a stale
                             * too-small `rs` is still wrong in its own right —
                             * fixed for consistency with the field-size path. */
                            int rs = sizeof_type_sym(ttd->type, sym->is_64bit, sym);
                            /* Also follow typedef → struct to get actual struct size */
                            if (ttd->type->base) {
                                const char *tbase3 = ttd->type->base;
                                const char *tbare3 = tbase3;
                                if (strncmp(tbare3,"struct ",7)==0) tbare3+=7;
                                else if (strncmp(tbare3,"union ",6)==0) tbare3+=6;
                                if (tbare3 != tbase3) {
                                    char sk3[256]; snprintf(sk3,sizeof sk3,"struct %s",tbare3);
                                    Symbol *ts3 = symtable_lookup(sym, sk3);
                                    if (ts3 && ts3->struct_size > 0) rs = ts3->struct_size;
                                    if (ts3 && ts3->struct_node) tstruct_align = symtable_compute_struct_alignment(sym, ts3->struct_node);
                                }
                            }
                            if (rs > tfsz) tfsz = rs;
                        }
                    }
                }
            }
            if (tfsz < 1) tfsz = 4;
            /* For alignment, use element size (not array total) */
            int elem_tsz = tfsz;
            if (f->field.array_size > 0) tfsz *= f->field.array_size;
            int talign = tstruct_align > 0 ? tstruct_align : (elem_tsz < 8 ? elem_tsz : 8);
            if (talign > 1) offset = (offset + talign-1) & ~(talign-1);
            if (getenv("SQUASH_FBO_DEBUG")) {
                fprintf(stderr, "[FBO-match] field='%s' i=%d struct='%s' offset=%d tfsz=%d talign=%d\n",
                        field_name, i, ss->name?ss->name:"?", offset, tfsz, talign);
            }
            return offset;
        }
        if (!is_union) {
            int fsz = 4; /* default: int-sized */
            int fstruct_align = 0; /* real alignment if struct/union-typed — see symtable_compute_struct_alignment() */
            if (f->field.type) {
                /* Same typedef-blindness fix as the matched-field branch
                 * above — resolves e.g. "Uint8"/"Uint16" to their real 1/2
                 * byte size instead of typeinfo_size()'s generic 4-byte
                 * default, so the running offset accumulated across
                 * preceding fields stays correct. */
                fsz = sizeof_type_sym(f->field.type, sym->is_64bit, sym);
                /* For struct/union fields, sizeof_type returns 4 (default).
                 * Look up the actual stored struct size from the symtable. */
                if (f->field.type->base && f->field.type->pointer_depth == 0) {
                    const char *fb = f->field.type->base;
                    const char *bare = fb;
                    if (strncmp(bare,"struct ",7)==0) bare+=7;
                    else if (strncmp(bare,"union ",6)==0) bare+=6;
                    if (bare != fb) {
                        /* Struct/union type */
                        char skey[256]; snprintf(skey,sizeof skey,"struct %s",bare);
                        Symbol *fs = symtable_lookup(sym, skey);
                        if (fs && fs->struct_size > 0) fsz = fs->struct_size;
                        if (fs && fs->struct_node) { fsz = symtable_sizeof_struct(sym, fs->struct_node); fstruct_align = symtable_compute_struct_alignment(sym, fs->struct_node); }
                    } else {
                        /* May be a typedef. This must handle pointer typedefs
                         * (e.g. "typedef void* HICON;") too — the FIELD's own
                         * pointer_depth is 0 (only the typedef definition
                         * carries the "*"), so gating this whole lookup on
                         * td->type->pointer_depth==0 (as this used to do)
                         * skipped resolution entirely for pointer-typedef'd
                         * fields, leaving fsz stuck at the wrong 4-byte
                         * default instead of 8 — corrupting every subsequent
                         * field's offset in structs like WNDCLASSEXA that
                         * mix several HANDLE-style typedefs in a row. */
                        Symbol *td = symtable_lookup(sym, fb);
                        if (td && td->kind == SYM_TYPEDEF && td->type && td->type->pointer_depth > 0) {
                            fsz = sym->is_64bit ? 8 : 4;
                        } else if (td && td->kind == SYM_TYPEDEF && td->type) {
                            const char *tb = td->type->base;
                            const char *tbare = tb;
                            if (strncmp(tbare,"struct ",7)==0) tbare+=7;
                            else if (strncmp(tbare,"union ",6)==0) tbare+=6;
                            if (tbare != tb) {
                                char tkey[256]; snprintf(tkey,sizeof tkey,"struct %s",tbare);
                                Symbol *tss = symtable_lookup(sym, tkey);
                                if (tss && tss->struct_size > 0) fsz = tss->struct_size;
                                if (tss && tss->struct_node) { fsz = symtable_sizeof_struct(sym, tss->struct_node); fstruct_align = symtable_compute_struct_alignment(sym, tss->struct_node); }
                            } else {
                                /* typeinfo_size() alone only resolves ONE
                                 * typedef hop and has no SymTable access, so
                                 * a chain like "SDL_ThreadID -> Uint64 ->
                                 * uint64_t" (real, exact SDL3 shape) silently
                                 * fell through to its "return 4" default at
                                 * the SECOND hop ("Uint64" isn't a name it
                                 * recognizes), OVERWRITING the correct value
                                 * `fsz` already held from the sizeof_type_sym()
                                 * call above (which DOES walk the full chain)
                                 * with this wrong, smaller one — corrupting
                                 * every subsequent field's offset in structs
                                 * with a 2+-hop-typedef'd field followed by
                                 * more fields (confirmed via a minimal repro
                                 * and the real SDL_Mutex struct: "sem"
                                 * silently landed 8 bytes too early, at
                                 * "owner"'s own offset). sizeof_type_sym()
                                 * already does this chain walk correctly —
                                 * just reuse it instead of re-deriving a
                                 * worse answer here. */
                                int ts2 = sizeof_type_sym(td->type, sym->is_64bit, sym);
                                if (ts2 > 0) fsz = ts2;
                            }
                        } else if (td && td->kind == SYM_STRUCT) {
                            if (td->struct_size > 0) fsz = td->struct_size;
                        }
                    }
                }
                if (fsz < 1) fsz = 4;
            }
            /* Use element size for alignment, total size for storage */
            int elem_fsz2 = fsz;
            if (f->field.array_size > 0) {
                fsz *= f->field.array_size;
                if (f->field.array_size2 > 0) fsz *= f->field.array_size2; /* T x[N][M] */
            }
            /* Align to element's natural alignment (max 8) */
            int align = fstruct_align > 0 ? fstruct_align : (elem_fsz2 < 8 ? elem_fsz2 : 8);
            if (align > 1) offset = (offset + align-1) & ~(align-1);
            offset += fsz;
        }
    }
    /* Not found at top level — try anonymous __anon_N members (C11 anonymous unions) */
    offset = 0;
    for (int i = 0; i < sd->struct_decl.nfields; i++) {
        ASTNode *f = sd->struct_decl.fields[i];
        if (!f || f->kind != AST_FIELD) continue;
        /* Check if this is an anonymous member (__anon_N) */
        if (f->field.name && strncmp(f->field.name, "__anon_", 7)==0 && f->field.type) {
            /* Find the struct for this anonymous member's type */
            const char *atype = f->field.type->base;
            if (atype) {
                const char *bare = atype;
                if (strncmp(bare,"struct ",7)==0) bare+=7;
                else if (strncmp(bare,"union ",6)==0) bare+=6;
                Symbol *asym = symtable_lookup(sym, atype);
                if (!asym) { char k[256]; snprintf(k,sizeof k,"struct %s",bare); asym=symtable_lookup(sym,k); }
                if (asym && asym->struct_node) {
                    ASTNode *asd = asym->struct_node;
                    int is_anon_union = (asd->struct_decl.is_union);
                    int anon_off = 0;
                    for (int j=0; j<asd->struct_decl.nfields; j++) {
                        ASTNode *af = asd->struct_decl.fields[j];
                        if (!af || af->kind!=AST_FIELD) continue;
                        if (af->field.name && strcmp(af->field.name, field_name)==0) {
                            return is_union ? 0 : (offset + (is_anon_union ? 0 : anon_off));
                        }
                        if (!is_anon_union) {
                            int fsz = af->field.type ? typeinfo_size(af->field.type,sym->is_64bit) : 4;
                            if (fsz<1) fsz=4;
                            if (af->field.array_size>0) { fsz*=af->field.array_size; if (af->field.array_size2>0) fsz*=af->field.array_size2; }
                            int align=fsz<8?fsz:8;
                            if (align>1) anon_off=(anon_off+align-1)&~(align-1);
                            anon_off+=fsz;
                        }
                    }
                }
            }
        }
        /* Advance offset past this field */
        if (!is_union) {
            int fsz = f->field.type ? typeinfo_size(f->field.type,sym->is_64bit) : 4;
            if (fsz<1) fsz=4;
            if (f->field.array_size>0) { fsz*=f->field.array_size; if (f->field.array_size2>0) fsz*=f->field.array_size2; }
            int align=fsz<8?fsz:8;
            if (align>1) offset=(offset+align-1)&~(align-1);
            offset+=fsz;
        }
    }
    return 0; /* field not found — safe fallback */
}

/* field_scalar_size — find `field_name` within obj's struct/union type and
 * return its plain scalar element size (ignoring array_size/array_size2),
 * setting *out_is2d if the field is a T x[N][M] 2D array. Used to size the
 * inner/second-dimension access of obj.field[i][j]. */
static int field_scalar_size(CodeGen *cg, ASTNode *obj, const char *field_name, int *out_is2d) {
    *out_is2d = 0;
    const char *tn = NULL;
    if (obj && obj->kind == AST_VAR) {
        Symbol *vs = symtable_lookup(cg->sym, obj->var.name);
        if (vs && vs->type) tn = vs->type->base;
    } else if (obj) {
        tn = resolve_node_type(cg->sym, obj);
    }
    if (!tn) return 4;
    char key[256];
    const char *bare_tn = tn;
    if (strncmp(bare_tn,"struct ",7)==0) bare_tn+=7;
    else if (strncmp(bare_tn,"union ",6)==0) bare_tn+=6;
    if (bare_tn == tn) {
        Symbol *td = symtable_lookup(cg->sym, tn);
        if (td && td->kind == SYM_TYPEDEF && td->type) {
            const char *tb = td->type->base;
            if (strncmp(tb,"struct ",7)==0) bare_tn = tb + 7;
            else if (strncmp(tb,"union ",6)==0) bare_tn = tb + 6;
            else bare_tn = tb;
        }
    }
    snprintf(key, sizeof key, "struct %s", bare_tn);
    Symbol *ss = symtable_lookup(cg->sym, key);
    if (!ss || !ss->struct_node) return 4;
    ASTNode *sd = ss->struct_node;
    for (int i = 0; i < sd->struct_decl.nfields; i++) {
        ASTNode *f = sd->struct_decl.fields[i];
        if (!f || f->kind != AST_FIELD || !f->field.name) continue;
        if (strcmp(f->field.name, field_name) != 0) continue;
        if (!f->field.type) return 4;
        *out_is2d = (f->field.array_size2 > 0);
        int sz = typeinfo_size(f->field.type, cg->is_64bit);
        return sz < 1 ? 1 : sz;
    }
    return 4;
}

/* pointer_pointee_size — returns >0 (the pointee's byte size) if `node` is
 * CONFIRMED to be a genuine pointer-typed expression (a plain variable with
 * pointer_depth>=1) — 0 otherwise (not a pointer, or type unresolvable).
 * Used by AST_BINARY's "+"/"-" codegen: squash's generic add/sub only ever
 * did raw integer arithmetic, with no notion that "ptr + i" in C means
 * "ptr + i*sizeof(*ptr)" — e.g. SDL3's own hashtable insert_item() does
 * "Item *candidate = table + idx;" (pointer arithmetic, not "table[idx]"
 * array-index syntax), which computed table+idx as a raw BYTE offset
 * instead of table+idx*sizeof(Item), so idx=2 landed 2 bytes into the
 * table instead of 2 whole elements in — a genuine, general squash
 * codegen gap (any C using "ptr + i" pointer-arithmetic syntax for a
 * non-char pointer was affected), not an SDL bug. Deliberately narrow
 * (AST_VAR only) — this is a targeted fix for the confirmed failure mode,
 * not a full pointer-type-tracking rewrite of the expression codegen. */
static int pointer_pointee_size_of_type(CodeGen *cg, TypeInfo *t) {
    if (!t || t->pointer_depth < 1) return 0;
    const char *base = t->base;
    if (base && (strcmp(base,"char")==0 || strcmp(base,"signed char")==0 ||
                 strcmp(base,"unsigned char")==0 || strcmp(base,"void")==0))
        return 0; /* byte-granular pointers: scale factor 1, nothing to do */
    if (t->pointer_depth >= 2) return cg->is_64bit ? 8 : 4; /* pointer-to-pointer: pointee is itself a pointer */
    TypeInfo tmp;
    tmp.base=base; tmp.pointer_depth=0; tmp.is_const=0; tmp.is_unsigned=0;
    tmp.array_size=-1; tmp.pointed_to=t->pointed_to;
    tmp.is_volatile=0; tmp.is_inline=0; tmp.is_extern=0; tmp.is_float=0;
    int sz = sizeof_type_sym(&tmp, cg->is_64bit, cg->sym);
    return sz>1 ? sz : 0; /* size==1 (or unresolved): no scaling needed/possible */
}
static int pointer_pointee_size(CodeGen *cg, ASTNode *node) {
    if (!node) return 0;
    if (node->kind == AST_VAR) {
        Symbol *s = symtable_lookup(cg->sym, node->var.name);
        if (!s || !s->type) return 0;
        if (s->array_size > 0) {
            /* Array-to-pointer decay: "arr + i" used OUTSIDE an immediate
             * "arr[i]" subscript (e.g. "wchar_t *p = arr + i;") must scale
             * by the array's element size, per C's array-to-pointer decay
             * rule — same as real pointer arithmetic. Previously this
             * returned 0 unconditionally on the assumption that AST_INDEX's
             * own "arr[i]" codegen (elem_size_of(), a separate code path
             * that never calls this function) needed the array case left
             * unscaled to avoid double-scaling — but that path doesn't
             * depend on this function at all, so leaving arrays unscaled
             * here only broke genuine "arr + i" pointer-arithmetic syntax:
             * e.g. "wchar_t *p = buf + 3;" advanced 3 BYTES instead of 3
             * wchar_t elements (6 bytes), corrupting anything written
             * through p afterward. */
            TypeInfo tmp = *s->type;
            tmp.pointer_depth = s->type->pointer_depth + 1;
            return pointer_pointee_size_of_type(cg, &tmp);
        }
        return pointer_pointee_size_of_type(cg, s->type);
    }
    if (node->kind == AST_MEMBER) {
        /* "obj.field + i" / "obj->field + i" / "field++" where field is a
         * pointer-typed struct member (e.g. SDL_hashtable.c's "table->table",
         * a "SDL_HashItem *" field) — the AST_VAR-only version above missed
         * this entirely, so "table->table + (hash_mask+1)" and any "++"/"--"
         * on such a field silently used scale factor 1 instead of the
         * pointee's real size. */
        TypeInfo *ft = field_type_of(cg, node->member.obj, node->member.field);
        return pointer_pointee_size_of_type(cg, ft);
    }
    return 0;
}

/* deref_struct_size — returns >0 (the struct/union's byte size) if
 * dereferencing `operand` (the pointer expression inside an AST_DEREF, e.g.
 * "p" in "*p") yields a struct/union VALUE rather than a scalar or another
 * pointer. Used to detect whole-struct assignment ("*a = *b;") so it can be
 * compiled as an address-to-address copy instead of squash's normal
 * single-register scalar assignment path, which silently truncated struct
 * copies to whatever fits in one register — e.g. SDL3's own hashtable
 * insert_item() does "*candidate = *item_to_insert;" for a
 * multi-field SDL_HashItem; only a few leading bytes ever actually landed,
 * leaving fields like "live"/"hash" at their zeroed-by-calloc default, so a
 * just-inserted item was immediately invisible to lookups. A general
 * squash codegen gap, not an SDL bug. */
static int deref_struct_size_of_type(CodeGen *cg, TypeInfo *t) {
    if (!t || t->pointer_depth!=1) return 0;
    const char *base = t->base;
    if (!base) return 0;
    if (strcmp(base,"char")==0||strcmp(base,"signed char")==0||strcmp(base,"unsigned char")==0||
        strcmp(base,"short")==0||strcmp(base,"unsigned short")==0) return 0;
    int is_struct = (strncmp(base,"struct ",7)==0 || strncmp(base,"union ",6)==0);
    if (!is_struct) {
        Symbol *td = symtable_lookup(cg->sym, base);
        if (td && td->kind==SYM_TYPEDEF && td->type && td->type->base &&
            (strncmp(td->type->base,"struct ",7)==0 || strncmp(td->type->base,"union ",6)==0))
            is_struct = 1;
    }
    if (!is_struct) return 0;
    TypeInfo tmp;
    tmp.base=base; tmp.pointer_depth=0; tmp.is_const=0; tmp.is_unsigned=0;
    tmp.array_size=-1; tmp.pointed_to=t->pointed_to;
    tmp.is_volatile=0; tmp.is_inline=0; tmp.is_extern=0; tmp.is_float=0;
    int sz = sizeof_type_sym(&tmp, cg->is_64bit, cg->sym);
    return sz>0 ? sz : 0;
}
static int deref_struct_size(CodeGen *cg, ASTNode *operand) {
    /* "s->type" (a pointer-typed struct FIELD reached via ->) is itself an
     * AST_MEMBER node, not AST_VAR — the walk-down loop below only ever
     * bottomed out at a plain variable, so "TypeInfo tmp = *s->type;" (a
     * VAR_DECL initializer shaped exactly like this, confirmed via a
     * minimal repro segfaulting on the resulting garbage struct) fell
     * through to `return 0`, silently truncating the struct copy to one
     * register's worth of bytes instead of copying the whole thing. Handle
     * this shape directly via field_type_of() rather than descending
     * further, since a member access's type is resolved independently of
     * whatever chain of exprs its own object expression is built from. */
    if (operand && operand->kind==AST_MEMBER) {
        TypeInfo *ft = field_type_of(cg, operand->member.obj, operand->member.field);
        return deref_struct_size_of_type(cg, ft);
    }
    ASTNode *v = operand;
    while (v && v->kind != AST_VAR) {
        if (v->kind==AST_UNARY && v->unary.operand) v = v->unary.operand;
        else if (v->kind==AST_BINARY && v->binary.left) v = v->binary.left;
        else if (v->kind==AST_ASSIGN && v->assign.lhs) v = v->assign.lhs;
        else return 0;
    }
    if (!v || v->kind!=AST_VAR) return 0;
    Symbol *ds = symtable_lookup(cg->sym, v->var.name);
    if (!ds || !ds->type || ds->type->pointer_depth!=1) return 0;
    const char *base = ds->type->base;
    if (!base) return 0;
    if (strcmp(base,"char")==0||strcmp(base,"signed char")==0||strcmp(base,"unsigned char")==0||
        strcmp(base,"short")==0||strcmp(base,"unsigned short")==0) return 0;
    int is_struct = (strncmp(base,"struct ",7)==0 || strncmp(base,"union ",6)==0);
    if (!is_struct) {
        Symbol *td = symtable_lookup(cg->sym, base);
        if (td && td->kind==SYM_TYPEDEF && td->type && td->type->base &&
            (strncmp(td->type->base,"struct ",7)==0 || strncmp(td->type->base,"union ",6)==0))
            is_struct = 1;
    }
    if (!is_struct) return 0;
    TypeInfo tmp;
    tmp.base=base; tmp.pointer_depth=0; tmp.is_const=0; tmp.is_unsigned=0;
    tmp.array_size=-1; tmp.pointed_to=ds->type->pointed_to;
    tmp.is_volatile=0; tmp.is_inline=0; tmp.is_extern=0; tmp.is_float=0;
    int sz = sizeof_type_sym(&tmp, cg->is_64bit, cg->sym);
    return sz>0 ? sz : 0;
}

/* var_struct_size — companion to deref_struct_size(), for the OTHER half
 * of whole-struct assignment: a plain (non-pointer) struct/union-typed
 * variable, e.g. "temp_item" in SDL3's own hashtable insert_item()'s
 * "temp_item = *candidate;" / "*item_to_insert = temp_item;" (a Robin
 * Hood displacement swap) — neither side of either statement is a
 * "*ptr1 = *ptr2" shape, so deref_struct_size() alone doesn't catch them,
 * but they need the exact same address-to-address copy treatment. */
static int var_struct_size(CodeGen *cg, ASTNode *var_node) {
    if (!var_node || var_node->kind != AST_VAR) return 0;
    Symbol *ds = symtable_lookup(cg->sym, var_node->var.name);
    if (!ds || !ds->type || ds->type->pointer_depth!=0) return 0;
    if (ds->array_size > 0 || ds->type->array_size > 0) return 0; /* arrays: leave alone */
    const char *base = ds->type->base;
    if (!base) return 0;
    int is_struct = (strncmp(base,"struct ",7)==0 || strncmp(base,"union ",6)==0);
    if (!is_struct) {
        Symbol *td = symtable_lookup(cg->sym, base);
        if (td && td->kind==SYM_TYPEDEF && td->type && td->type->base &&
            (strncmp(td->type->base,"struct ",7)==0 || strncmp(td->type->base,"union ",6)==0))
            is_struct = 1;
    }
    if (!is_struct) return 0;
    int sz = sizeof_type_sym(ds->type, cg->is_64bit, cg->sym);
    return sz>0 ? sz : 0;
}

/* struct_copy_size_of / struct_copy_addr_of — shared by the whole-struct
 * assignment fast path in AST_ASSIGN codegen. size_of returns >0 if `node`
 * (an assignment's lhs or rhs) denotes a struct/union VALUE — either
 * "*ptr" (AST_DEREF of a struct pointer) or a plain struct-typed variable
 * (AST_VAR, not a pointer). addr_of emits code leaving that value's
 * ADDRESS in RAX, needed either way since the copy is done address-to-
 * address rather than through a value-holding register. */
static int struct_copy_size_of(CodeGen *cg, ASTNode *node) {
    if (!node) return 0;
    if (node->kind==AST_DEREF) return deref_struct_size(cg, node->deref.operand);
    if (node->kind==AST_VAR) return var_struct_size(cg, node);
    if (node->kind==AST_MEMBER) {
        /* A nested struct-typed FIELD — "outer.inner = other;" (e.g.
         * SDL_Storage's "storage->iface = *some_iface;"-equivalent shape:
         * a struct-typed member, not a pointer field). Neither AST_DEREF
         * (LHS isn't itself a pointer dereference) nor AST_VAR (LHS is a
         * member access, not a bare variable) matched this, so it fell
         * through to the generic single-register assignment path, which
         * silently copied only the first 4/8 bytes of the nested struct —
         * e.g. a "SDL_StorageInterface iface;" field's leading Uint32
         * "version" copied fine, but every function-pointer field after it
         * stayed zeroed, making every storage-interface method silently
         * appear "unsupported" (null function pointer) after such an
         * assignment. */
        TypeInfo *ft = field_type_of(cg, node->member.obj, node->member.field);
        if (!ft || ft->pointer_depth != 0 || !ft->base) return 0;
        const char *base = ft->base;
        int is_struct = (strncmp(base,"struct ",7)==0 || strncmp(base,"union ",6)==0);
        if (!is_struct) {
            Symbol *td = symtable_lookup(cg->sym, base);
            if (td && td->kind==SYM_TYPEDEF && td->type && td->type->base &&
                (strncmp(td->type->base,"struct ",7)==0 || strncmp(td->type->base,"union ",6)==0))
                is_struct = 1;
        }
        if (!is_struct) return 0;
        int sz = sizeof_type_sym(ft, cg->is_64bit, cg->sym);
        return sz>0 ? sz : 0;
    }
    if (node->kind==AST_INDEX) {
        /* A struct/union-typed ARRAY ELEMENT — "arr[i] = other;" (e.g. a
         * software renderer building up a vertex/command array one struct
         * at a time, or a C99 compound literal assigned into an array
         * slot: "rects[i] = (SDL_FRect){...};"). Like the AST_MEMBER case
         * above, this shape matched neither AST_DEREF nor AST_VAR, so it
         * fell through to the generic single-register assignment path and
         * silently copied only the first 4/8 bytes of the element,
         * leaving every field after the first zeroed/stale. */
        /* BUT: resolve_node_type() only ever returns the element's BARE
         * struct/typedef NAME — it strips "struct "/"union " prefixes and
         * resolves one typedef hop, but never reports how many pointer
         * levels the element actually has. For a real POINTER array
         * ("SDL_DisabledEventBlock *arr[256];", e.g. SDL3's own
         * SDL_disabled_events), each element is an 8-byte pointer, not a
         * struct value — yet resolve_node_type() still returns
         * "SDL_DisabledEventBlock" here (the pointee's name), and this
         * function used to build a fake TypeInfo with pointer_depth
         * hardcoded to 0 regardless, misclassifying "arr[i]" as a >16-byte
         * struct-by-value argument. That sent a call like
         * "SDL_free(arr[i])" down the MEMORY-class SysV struct-passing
         * path instead of the ordinary single-register pointer path: it
         * copied 32 bytes starting at &arr[i] (spilling into arr[i+1..3])
         * onto the stack and called SDL_free() with a mis-marshaled
         * argument — confirmed as the exact cause of a real "free():
         * invalid size" abort in SDL3's SDL_StopEventLoop() (real,
         * unmodified SDL3 source; SDL_free(SDL_disabled_events[i]) in its
         * "Clear disabled event state" loop), where the passed "pointer"
         * ended up pointing into the caller's own stack instead of a real
         * heap allocation. Check the array variable's own declared
         * pointer_depth first — if it's already >0 (the array holds
         * pointers to this type, not the struct itself), this is an
         * ordinary scalar argument, not a struct-by-value one. */
        /* The check above only ever covered "arr[i]" where arr is a bare
         * variable. The exact same misclassification happens just as
         * easily one level deeper — "ptr->field[i]" / "x.y.field[i]" —
         * whenever "field" is itself declared as a pointer-to-pointer
         * (e.g. ast.h's own "ASTNode **stmts;" inside AST_BLOCK's node,
         * indexed everywhere as "n->block.stmts[i]"). Confirmed as a
         * REAL, currently-active squash self-hosting bug: compiling
         * squash's own codegen.c (specifically THIS FILE's own
         * "codegen_stmt(cg, n->block.stmts[i])" call in the AST_BLOCK
         * case) sent stmts[i] — a single 8-byte ASTNode* — down this same
         * MEMORY-class struct-by-value path, copying ~70+ bytes starting
         * at &stmts[i] onto the stack as the call argument. Caught via a
         * custom guard-page allocator (every heap allocation placed snug
         * against an unmapped page): the resulting out-of-bounds read
         * faulted immediately, at the exact "mov 0x8(%rax),%rcx" one
         * instruction past the single legitimate 8-byte pointer value —
         * previously this only ever corrupted whatever real allocation
         * happened to sit right after stmts[i] in memory, silently and
         * non-deterministically (a self-compiled squash would compile
         * trivial one-statement programs fine but reliably mangle
         * anything with more going on, exactly the "gen1 fails on
         * ast.c but not on tiny test programs" self-hosting symptom).
         * A pointer_depth >= 2 array/field can NEVER index down to a
         * struct-by-value element (indexing removes exactly one pointer
         * level, so the result is still a pointer, depth >= 1) — check
         * that first, however the array expression is spelled. */
        if (node->index.array) {
            ASTNode *arr = node->index.array;
            if (arr->kind == AST_VAR) {
                Symbol *avs = symtable_lookup(cg->sym, arr->var.name);
                if (avs && avs->type && avs->type->pointer_depth > 0) return 0;
            } else if (arr->kind == AST_MEMBER) {
                TypeInfo *ft = field_type_of(cg, arr->member.obj, arr->member.field);
                if (ft && ft->pointer_depth > 0) return 0;
            }
        }
        const char *tn = resolve_node_type(cg->sym, node);
        if (!tn) return 0;
        TypeInfo tmp;
        tmp.base=tn; tmp.pointer_depth=0; tmp.is_const=0; tmp.is_unsigned=0;
        tmp.array_size=-1; tmp.pointed_to=NULL;
        tmp.is_volatile=0; tmp.is_inline=0; tmp.is_extern=0; tmp.is_float=0;
        int sz = sizeof_type_sym(&tmp, cg->is_64bit, cg->sym);
        return sz>0 ? sz : 0;
    }
    return 0;
}
static void struct_copy_addr_of(CodeGen *cg, ASTNode *node) {
    if (node->kind==AST_DEREF) codegen_expr(cg, node->deref.operand);
    else codegen_lvalue(cg, node);
}

/* =========================================================================
 * SysV x86-64 struct-by-value call arguments (the "MEMORY class" of the
 * real ABI). Every existing SysV call-argument codegen path in this file
 * (AST_CALL and AST_FUNC_PTR_CALL) modeled every argument as ONE 8-byte
 * int-or-float register slot, with no concept of a struct/union passed by
 * value at all -- confirmed via a minimal repro ("struct { double x,y; }
 * origin,size;" packed into a 4-double struct passed by value to a
 * function summing all 4 fields returned a wrong, truncated answer). Real
 * C code that does this is rare in most C codebases but is the ENTIRE
 * calling convention of Objective-C's objc_msgSend() (e.g.
 * -[NSWindow initWithContentRect:styleMask:backing:defer:] takes an NSRect
 * -- 4 doubles, 32 bytes -- by value), needed for a real Cocoa/AppKit
 * window backend.
 *
 * Only structs/unions BIGGER than 16 bytes are handled here (the real
 * ABI's unconditional "MEMORY class" case: no per-eightbyte INTEGER/SSE
 * register classification needed, just a byte-for-byte copy onto the
 * outgoing stack-argument area at the argument's natural position, never
 * consuming an integer or SSE register slot). Structs of 16 bytes or less
 * (e.g. NSPoint/NSSize) still fall through to the existing, unfixed
 * scalar-only path below -- not needed by anything this build calls yet,
 * and correctly classifying THOSE requires real per-eightbyte SSE/INTEGER
 * inspection this doesn't attempt.
 *
 * This is deliberately a SEPARATE code path from the original nreg<=6
 * model, entered only when at least one argument actually needs it (see
 * each call site's own "has a large struct arg" check) -- it changes
 * stack-argument LAYOUT relative to the original model (a struct argument
 * shifts where any later stack-bound scalar argument lands), so every
 * existing call with no large-struct argument keeps its original,
 * already-tested codegen completely unchanged. */
static void classify_sysv_call_args(CodeGen *cg, ASTNode **args, int argc,
                                     int *mem_size, int *is_float, int *uses_reg,
                                     int *reg_index, int *scratch_slot, int *stack_slot,
                                     int *out_nreg, int *out_nstack) {
    int ireg=0, freg=0, combined=0, nstack=0;
    int i;
    for (i = 0; i < argc; i++) {
        int sz = struct_copy_size_of(cg, args[i]);
        mem_size[i] = (sz > 16) ? sz : 0;
        if (mem_size[i] > 0) {
            uses_reg[i] = 0;
            is_float[i] = 0;
        } else {
            is_float[i] = codegen_is_float_expr(cg, args[i]);
            if (combined < 6) {
                uses_reg[i] = 1;
                scratch_slot[i] = combined;
                reg_index[i] = is_float[i] ? freg++ : ireg++;
                combined++;
            } else {
                uses_reg[i] = 0;
            }
        }
        if (!uses_reg[i]) {
            stack_slot[i] = nstack;
            nstack += (mem_size[i] > 0) ? (mem_size[i] + 7) / 8 : 1;
        }
    }
    *out_nreg = combined;
    *out_nstack = nstack;
}

/* Returns 1 if any argument is a struct/union-by-value bigger than 16
 * bytes -- see classify_sysv_call_args's own comment for why that's the
 * trigger for this whole separate code path. */
static int call_has_large_struct_arg(CodeGen *cg, ASTNode **args, int argc) {
    int i;
    for (i = 0; i < argc; i++) {
        if (struct_copy_size_of(cg, args[i]) > 16) return 1;
    }
    return 0;
}

/* Emits the argument-evaluation + register-loading half of a SysV x86-64
 * call, given classify_sysv_call_args's output. Leaves RSP already
 * adjusted by the caller (frame bytes reserved, stack-args area at [RSP+0]
 * and register-value scratch area at [RSP+reg_scratch_base]) and every
 * register loaded; the caller still emits the actual `call` itself and the
 * matching `asm_add_rsp(a, frame)` afterward -- this only replaces the
 * argument-setup portion, so each call site's own (already correct and
 * tested) call-target resolution/emission code is reused unchanged.
 * `sym_or_null` mirrors the two call sites' different single-precision-
 * float-narrowing checks: a named function's declared parameter types
 * (param_is_single_float) for AST_CALL, vs. a function-pointer call's own
 * expression-width heuristic (float_expr_width) for AST_FUNC_PTR_CALL,
 * matching what each already did before this path existed. */
static void emit_sysv_struct_call_args(CodeGen *cg, ASTNode **args, int argc,
                                        int *mem_size, int *is_float, int *uses_reg,
                                        int *reg_index, int *scratch_slot, int *stack_slot,
                                        int reg_scratch_base, Symbol *sym_or_null) {
    Assembler *a = cg->asm_;
    int i, w;
    for (i = 0; i < argc; i++) {
        if (uses_reg[i]) {
            if (is_float[i]) {
                codegen_float_expr(cg, args[i]);
                asm_movsd_store(a, REG_RSP, reg_scratch_base + scratch_slot[i]*8, 0);
            } else {
                codegen_expr(cg, args[i]);
                asm_mov_mem_reg(a, REG_RSP, reg_scratch_base + scratch_slot[i]*8, REG_RAX);
            }
        } else if (mem_size[i] > 0) {
            struct_copy_addr_of(cg, args[i]); /* struct's address -> RAX */
            int nwords = (mem_size[i] + 7) / 8;
            for (w = 0; w < nwords; w++) {
                asm_mov_reg_mem(a, REG_RCX, REG_RAX, w*8);
                asm_mov_mem_reg(a, REG_RSP, stack_slot[i]*8 + w*8, REG_RCX);
            }
        } else {
            codegen_expr(cg, args[i]);
            asm_mov_mem_reg(a, REG_RSP, stack_slot[i]*8, REG_RAX);
        }
    }
#define SC_IREG(i) ((i)==0?REG_RDI:(i)==1?REG_RSI:(i)==2?REG_RDX:(i)==3?REG_RCX:(i)==4?REG_R8:REG_R9)
    for (i = 0; i < argc; i++) {
        if (!uses_reg[i]) continue;
        if (is_float[i]) {
            asm_movsd_load(a, reg_index[i], REG_RSP, reg_scratch_base + scratch_slot[i]*8);
            int narrow = sym_or_null ? param_is_single_float(sym_or_null, i, cg->sym, cg->is_64bit)
                                      : (float_expr_width(cg, args[i]) == 4);
            if (narrow) asm_cvtsd2ss(a, reg_index[i], reg_index[i]);
        } else {
            asm_mov_reg_mem(a, SC_IREG(reg_index[i]), REG_RSP, reg_scratch_base + scratch_slot[i]*8);
        }
    }
#undef SC_IREG
}

/* expr_is_unsigned_int — best-effort check for whether an integer
 * expression's declared type is unsigned, used to choose between signed
 * (idiv) and unsigned (div) division/modulo. `/`/`%`/`/=`/`%=` previously
 * always used idiv regardless of operand signedness — harmless for small
 * positive values (signed and unsigned division agree there), but wrong
 * for any unsigned 64-bit value with the high bit set: idiv sign-extends
 * the dividend via cqo, so e.g. "0xFFFFFFFFFFFFFFFFULL % 10" (an unsigned
 * value with every bit set) got sign-extended into RDX:RAX as -1:-1 and
 * computed the SIGNED answer "-1 % 10 == -1" (0xFFFFFFFFFFFFFFFF again,
 * unchanged) instead of the correct unsigned answer (5) — and "/ 10"
 * likewise gave signed "-1 / 10 == 0" instead of the correct
 * 1844674407370955161. This broke stdlib/SDL_string.c's own
 * SDL_ulltoa()/SDL_lltoa() (the printf-family %llu/%zu digit-extraction
 * loop, "value % radix" + "value /= radix"), silently corrupting any
 * SDL_Log/SDL_snprintf formatting of a Uint64 >= 2^63 — confirmed via an
 * isolated repro completely unrelated to SDL (a bare "unsigned long long
 * % 10" and "/ 10" on 0xFFFFFFFFFFFFFFFFULL). Defaults to "signed" (0)
 * for any shape not positively identified here, matching squash's prior
 * always-signed behavior — this only ADDS correct unsigned handling for
 * shapes we can resolve, never removes existing signed behavior. */
/* type_is_unsigned_resolved — like is_byte_sized_stdint()/
 * is_short_sized_stdint() above (same one-hop-isn't-enough problem, same
 * bounded-chain-walk fix): a TypeInfo's OWN is_unsigned flag is only
 * reliably set when the type was written out literally as "unsigned ..."
 * at the point of declaration. A multi-hop typedef alias — e.g. SDL3's
 * own "typedef uint64_t Uint64;" where "uint64_t" is itself
 * "typedef unsigned long long uint64_t;" — does NOT inherit is_unsigned
 * through the chain: a variable declared "Uint64 length;" has
 * type->base=="Uint64", is_unsigned==0 (never explicitly set), so a naive
 * single-level check misses it entirely. This made "length > SDL_SIZE_MAX"
 * (GENERIC_WriteStorageFile's very first line) use SIGNED comparison even
 * after the sibling div/mod and branch-condition fixes above, because
 * neither operand's is_unsigned flag was directly true — only reachable
 * by resolving "Uint64" -> "uint64_t" -> "unsigned long long". */
static int type_is_unsigned_resolved(SymTable *sym, TypeInfo *type) {
    if (!type || type->pointer_depth != 0 || !type->base) return 0;
    if (type->is_unsigned) return 1;
    const char *base = type->base;
    for (int hops=0; base && hops<8; hops++) {
        if (strncmp(base,"unsigned ",9)==0) return 1;
        if (strcmp(base,"uint8_t")==0||strcmp(base,"uint16_t")==0||strcmp(base,"uint32_t")==0||
            strcmp(base,"uint64_t")==0||strcmp(base,"size_t")==0||strcmp(base,"uintptr_t")==0) return 1;
        Symbol *td = symtable_lookup(sym, base);
        if (td && td->kind==SYM_TYPEDEF && td->type && td->type->pointer_depth==0) {
            if (td->type->is_unsigned) return 1;
            base = td->type->base;
        } else break;
    }
    return 0;
}

static int expr_is_unsigned_int(CodeGen *cg, ASTNode *n) {
    if (!n) return 0;
    switch (n->kind) {
    case AST_VAR: {
        Symbol *s = symtable_lookup(cg->sym, n->var.name);
        if (!s || !s->type) return 0;
        /* AST_INDEX's own "esz==1 byte load: sign- or zero-extend?" check
         * (see its own comment) calls this with n->index.array -- i.e. THE
         * POINTER/ARRAY BEING INDEXED, not a plain scalar value -- to ask
         * "is the ELEMENT type unsigned?", not "is this pointer value
         * unsigned?" (a question that doesn't meaningfully apply to a
         * pointer at all). type_is_unsigned_resolved() bails to 0
         * (`pointer_depth != 0`) for exactly this case: is_unsigned is
         * still set correctly on a pointer's own TypeInfo (it describes
         * the POINTEE's signedness, e.g. "unsigned char *" has
         * is_unsigned=1, pointer_depth=1 -- same field struct-member reads
         * already trust regardless of pointer_depth, see AST_MEMBER's
         * "field_is_unsigned = fty->is_unsigned;" a few hundred lines
         * below), it's just that resolved-typedef helper's OWN guard was
         * never meant to gate this call site. Read it directly instead of
         * routing through that helper when indexing a pointer/array --
         * confirmed as the actual cause of a real, reproducible bug: "p[i]"
         * for a `const unsigned char *p` parameter with p[i] >= 0x80
         * silently sign-extended to a 64-bit value with all-1s upper bits
         * (e.g. 0x99 became 0xFFFFFFFFFFFFFF99), corrupting any OR/shift
         * expression combining several such loads -- a plain LOCAL array
         * of the same element type didn't hit this (its Symbol's own
         * pointer_depth is 0, not the pointer-PARAMETER case this fixes).
         * Every other call site of expr_is_unsigned_int() passes a plain
         * scalar-value expression (division/modulo operands, assignment
         * LHS), where a real C variable's pointer_depth is always 0 anyway
         * (you can't divide by a pointer), so this branch is inert for
         * them -- safe to widen here without touching type_is_unsigned_
         * resolved() itself, which other, genuinely pointer-depth-sensitive
         * callers still rely on bailing to 0 for an actual pointer VALUE. */
        if (s->type->pointer_depth != 0) return s->type->is_unsigned ? 1 : 0;
        return type_is_unsigned_resolved(cg->sym, s->type);
    }
    case AST_CAST:
        return n->cast.type ? type_is_unsigned_resolved(cg->sym, n->cast.type) : 0;
    case AST_MEMBER: {
        /* Same pointer_depth!=0 gap as this function's own AST_VAR case
         * just above (see its long comment for the full story) -- a
         * struct field declared as a pointer, e.g. "unsigned char *plane;"
         * indexed as "comp->plane[i]", reaches here with n->member.obj
         * itself unused directly: field_type_of() returns THE FIELD'S OWN
         * TypeInfo (pointer_depth==1, is_unsigned describing the POINTEE),
         * which type_is_unsigned_resolved() then unconditionally bails to
         * 0 for (its `pointer_depth != 0` guard, meant for a genuine
         * pointer VALUE, not "what's the element type of what this
         * indexes"). Confirmed as a real, reproducible bug distinct from
         * (but the same root cause as) the AST_VAR fix above: a JPEG
         * decoder's "unsigned char *plane" component buffer, read through
         * a struct pointer as "comp->plane[i]", sign-extended any byte
         * >=0x80 into a huge/negative value instead of the real 0..255
         * unsigned sample -- corrupting chroma (Cr) reconstruction for
         * exactly the pixel values that needed the high bit set. */
        TypeInfo *ft = field_type_of(cg, n->member.obj, n->member.field);
        if (!ft) return 0;
        if (ft->pointer_depth != 0) return ft->is_unsigned ? 1 : 0;
        return type_is_unsigned_resolved(cg->sym, ft);
    }
    default:
        return 0;
    }
}

/* elem_size_of — element size for an array/pointer expression.
 * Handles AST_VAR (stack arrays, pointer vars) and AST_MEMBER (struct field arrays). */
static int elem_size_of(CodeGen *cg, ASTNode *arr_expr) {
    if (!arr_expr) return 4;

    /* Direct indexing into a string literal — `"0123456789ABCDEF"[i]` —
     * a very common C idiom (hex/lookup tables passed inline rather than
     * through a named `const char *` variable). AST_STRING had no case
     * here at all, so it fell through to the generic "unknown = 4"
     * default below, scaling the index by 4 bytes instead of 1: `lit[1]`
     * read the byte 4 past `lit[0]` instead of 1 past it, `lit[2]` read 8
     * bytes past, etc. — indexing through an equivalent `const char *`
     * variable pointing at the same literal was unaffected (AST_VAR
     * already had a correct case below), which is what made this so easy
     * to miss: only the *inline* literal form broke. */
    if (arr_expr->kind == AST_STRING) return arr_expr->str.is_wide ? 2 : 1;

    /* Cast-to-pointer base — `((char*)&s)[i] = 0` and similar. Falling
     * through to the generic "unknown = 4" default here silently turned
     * every such indexed store/load into a 4-byte access regardless of the
     * cast's actual target type: a common idiom like zeroing a struct via
     * `for (i=0;i<sizeof(s);i++) ((char*)&s)[i]=0;` would then stomp 4 bytes
     * per iteration instead of 1, overrunning the struct into neighboring
     * stack slots (return address included, once the struct is large enough
     * that the overrun escapes its 16-byte-aligned padding). */
    if (arr_expr->kind == AST_CAST && arr_expr->cast.type) {
        TypeInfo *ct = arr_expr->cast.type;
        if (ct->pointer_depth > 0) {
            TypeInfo tmp;
            tmp.base          = ct->base;
            tmp.pointer_depth = ct->pointer_depth - 1;
            tmp.is_const      = ct->is_const;
            tmp.is_unsigned   = ct->is_unsigned;
            tmp.array_size    = -1;
            tmp.pointed_to    = ct->pointed_to;
            tmp.is_volatile   = ct->is_volatile;
            tmp.is_inline     = ct->is_inline;
            tmp.is_extern     = ct->is_extern;
            tmp.is_float      = ct->is_float;
            int sz = sizeof_type_sym(&tmp, cg->is_64bit, cg->sym);
            return sz < 1 ? 1 : sz;
        }
    }

    /* Dereferenced pointer-to-pointer base — "(*pp)[i]" where pp is T** (or
     * deeper), e.g. a "T ***list" out-parameter's "(*list)[i] = val;" idiom
     * (this project's own parser_new4.c builds initializer-list arrays this
     * exact way: "(*elems)[(*ne)++] = ParseAssignment(p);"). *pp has one
     * fewer pointer level than pp itself, and indexing it strips one more —
     * so the element size comes from pp's own declared type with pointer_depth
     * reduced by TWO, not one. Falling through to the generic "unknown = 4"
     * default (there was no AST_DEREF case here at all) silently treated
     * every such pointer-sized element as 4 bytes regardless of its real
     * size: both the per-element STRIDE used to compute &(*pp)[i] and the
     * WIDTH of the store/load itself came out wrong, so a "T *pp[]" of
     * 8-byte pointer elements got each element's low 4 bytes written 4
     * bytes apart instead of 8 — confirmed via a minimal repro
     * ("(*elems)[(*ne)++] = val;" with elems a "Node ***" parameter and val
     * a "Node *") corrupting every element from the second one on and
     * eventually crashing when something dereferenced the resulting
     * Frankenstein'd pointer. Only the common, directly-declared-variable
     * operand is resolved here (matching this function's other branches,
     * which are similarly AST_VAR-first); anything else falls through to
     * the generic default below exactly as before this fix. */
    if (arr_expr->kind == AST_DEREF && arr_expr->deref.operand &&
        arr_expr->deref.operand->kind == AST_VAR) {
        Symbol *pvs = symtable_lookup(cg->sym, arr_expr->deref.operand->var.name);
        if (pvs && pvs->type && pvs->type->pointer_depth >= 2) {
            TypeInfo tmp;
            tmp.base          = pvs->type->base;
            tmp.pointer_depth = pvs->type->pointer_depth - 2;
            tmp.is_const      = pvs->type->is_const;
            tmp.is_unsigned   = pvs->type->is_unsigned;
            tmp.array_size    = -1;
            tmp.pointed_to    = pvs->type->pointed_to;
            tmp.is_volatile   = pvs->type->is_volatile;
            tmp.is_inline     = pvs->type->is_inline;
            tmp.is_extern     = pvs->type->is_extern;
            tmp.is_float      = pvs->type->is_float;
            int sz = sizeof_type_sym(&tmp, cg->is_64bit, cg->sym);
            return sz < 1 ? 1 : sz;
        }
    }

    /* Nested index — arr[i][j]: `arr_expr` here is the OUTER index node
     * (arr[i]) itself, meaning we're sizing the INNER/second-dimension
     * access. If the outer index's own base is a 2D array (T x[N][M]),
     * the outer index already consumed the row dimension, so this inner
     * access needs the plain scalar element size, not the row stride. */
    if (arr_expr->kind == AST_INDEX) {
        ASTNode *base = arr_expr->index.array;
        if (base && base->kind == AST_VAR) {
            Symbol *bsym = symtable_lookup(cg->sym, base->var.name);
            if (bsym && bsym->array_size2 > 0 && bsym->type) {
                /* typeinfo_size() alone can't resolve typedef'd element
                 * types (e.g. "typedef void (*Conv)(...); Conv x[N][M];" —
                 * a function-pointer typedef, whose own "*" lives on the
                 * typedef's underlying TypeInfo, not on x's, exactly like
                 * the PFNGLXFOOPROC struct-field case handled elsewhere in
                 * this file); it hardcodes 4 bytes for any base name it
                 * doesn't recognize as a primitive, understating a real
                 * 8-byte pointer element to 4. sizeof_type_sym() does the
                 * same lookup PLUS a typedef/struct symbol-table resolution
                 * pass, so use it here too — same fix class as this
                 * function's own struct/typedef handling a few lines below
                 * for the 1D case. Confirmed via a minimal 2x4 array-of-
                 * function-pointers repro: without this, consecutive
                 * elements' addresses were computed 4 bytes apart instead
                 * of 8, so every pair of adjacent function pointers packed
                 * into a single 8-byte slot (the low 4 bytes of each
                 * corrupting/replacing the other) — this was the second,
                 * separate root cause (after the AST_DEREF field-offset
                 * bug) behind real SDL3's channel_converters[8][8] audio
                 * channel-conversion table calling through NULL/garbage
                 * function pointers. */
                /* bsym->type carries the OUTER array's own array_size (the
                 * first dimension, e.g. 2 for Conv[2][4]) — sizeof_type_sym
                 * multiplies by that if left as-is (it's meant for "give me
                 * the whole array's byte size", not "give me one scalar
                 * element's size"), so strip it to size just the element. */
                TypeInfo elem_ti = *bsym->type;
                elem_ti.array_size = 0;
                int sz = sizeof_type_sym(&elem_ti, cg->is_64bit, cg->sym);
                return sz < 1 ? 1 : sz;
            }
            /* Not a real 2D stack array — a genuine pointer-to-pointer
             * variable instead (T **pp; pp[i][j];, e.g. SDL3's own
             * "const void * const *channel_buffers" audio-planar-data
             * arrays). base[i] (pp[i]) yields a T* value; indexing THAT
             * ([j]) needs sizeof(T), not the generic 4-byte default this
             * function falls back to when nothing above matches. Strip
             * two pointer levels (one for base[i], one more for the [j]
             * that's actually being sized here) rather than one, since
             * this branch sizes the SECOND dimension access. Confirmed via
             * SDL3's real SDL_audiocvt.c InterleaveAudioChannelsGeneric8's
             * "srcs[channel][frame]" (srcs is "const Uint8 * const *"):
             * without this, every such nested pointer-array byte read
             * silently became a 4-byte read of adjacent bytes instead. */
            if (bsym && bsym->array_size2 <= 0 && bsym->type && bsym->type->pointer_depth >= 2) {
                TypeInfo tmp;
                tmp.base          = bsym->type->base;
                tmp.pointer_depth = bsym->type->pointer_depth - 2;
                tmp.is_const      = bsym->type->is_const;
                tmp.is_unsigned   = bsym->type->is_unsigned;
                tmp.array_size    = -1;
                tmp.pointed_to    = bsym->type->pointed_to;
                tmp.is_volatile   = bsym->type->is_volatile;
                tmp.is_inline     = bsym->type->is_inline;
                tmp.is_extern     = bsym->type->is_extern;
                tmp.is_float      = bsym->type->is_float;
                if (tmp.pointer_depth > 0) return cg->is_64bit ? 8 : 4;
                int sz = sizeof_type_sym(&tmp, cg->is_64bit, cg->sym);
                return sz < 1 ? 1 : sz;
            }
        } else if (base && base->kind == AST_MEMBER) {
            int is2d = 0;
            int scalar_sz = field_scalar_size(cg, base->member.obj, base->member.field, &is2d);
            if (is2d) return scalar_sz;
        }
    }

    /* Case 1: simple variable — arr[i] or ptr[i] */
    if (arr_expr->kind == AST_VAR) {
        Symbol *asym = symtable_lookup(cg->sym, arr_expr->var.name);
        if (asym && asym->array_size2 > 0) {
            /* T x[N][M]: x[i] yields row i (an array of M elements) —
             * indexing it needs the row stride, not the scalar size.
             * Same typedef-resolution gap as this function's other
             * array_size2 branch just above (for the INNER/second-
             * dimension access): typeinfo_size() alone can't see through
             * a function-pointer typedef like "typedef void (*Conv)(...);
             * Conv x[N][M];", understating its real 8-byte element/row
             * stride to 4. Use sizeof_type_sym() (typedef/struct-aware)
             * instead — without this fix too, the FIRST-dimension (row)
             * stride was still wrong even after fixing the second-
             * dimension case alone, so real SDL3's audio
             * channel_converters[8][8] table still silently packed rows
             * incorrectly and called through garbage/NULL function
             * pointers, just with a different corruption pattern. */
            int base_elem;
            if (asym->type) {
                /* Same array_size-stripping requirement as the sibling
                 * fix above: asym->type carries the declared array's own
                 * (first-dimension) array_size, which sizeof_type_sym()
                 * would otherwise multiply in here too, double-counting
                 * it on top of this function's own "* array_size2" below. */
                TypeInfo elem_ti = *asym->type;
                elem_ti.array_size = 0;
                base_elem = sizeof_type_sym(&elem_ti, cg->is_64bit, cg->sym);
            } else {
                base_elem = 1;
            }
            if (base_elem < 1) base_elem = 1;
            return base_elem * asym->array_size2;
        }
        if (asym && asym->type) {
            int orig_pd = asym->type->pointer_depth;
            TypeInfo tmp;
            tmp.base         = asym->type->base;
            tmp.pointer_depth= (orig_pd > 0) ? orig_pd-1 : 0;
            tmp.is_const     = asym->type->is_const;
            tmp.is_unsigned  = asym->type->is_unsigned;
            tmp.array_size   = -1;
            tmp.pointed_to   = asym->type->pointed_to;
            tmp.is_volatile  = asym->type->is_volatile;
            tmp.is_inline    = asym->type->is_inline;
            tmp.is_extern    = asym->type->is_extern;
            tmp.is_float     = asym->type->is_float;
            /* If original pointer_depth > 0 and the element type is itself a pointer
             * (e.g. array of function pointers: int (*fps[3])()), return pointer size. */
            if (orig_pd > 0 && tmp.pointer_depth > 0) return cg->is_64bit ? 8 : 4;
            /* Also: if element stride is 0 (void*) or the base after stripping is still
             * a pointer context, use pointer size for func-ptr arrays.
             * Heuristic: if orig_pd==1 AND asym is a stack array (array_size>0),
             * each element IS a pointer (8 bytes in 64-bit, 4 in 32-bit). */
            if (orig_pd == 1 && asym->array_size > 0) return cg->is_64bit ? 8 : 4;
            /* Use sizeof_type_sym to resolve typedef-based struct sizes */
            return sizeof_type_sym(&tmp, cg->is_64bit, cg->sym);
        }
    }

    /* Case 2: struct/union field — obj.field[i]
     * Look up the field's declared type in the struct definition.             */
    if (arr_expr->kind == AST_MEMBER) {
        ASTNode *obj = arr_expr->member.obj;
        /* Resolve the struct/union type of obj (handle both VAR and pointer-via-arrow).
         * For nested chains like prog->program.decls, use resolve_node_type. */
        const char *tn = NULL;
        if (obj && obj->kind == AST_VAR) {
            Symbol *vs = symtable_lookup(cg->sym, obj->var.name);
            if (vs && vs->type) tn = vs->type->base;
        } else if (obj) {
            /* Nested member / index / arrow chain — resolve via AST walk */
            tn = resolve_node_type(cg->sym, obj);
        }
        if (!tn) return 4;
        /* Resolve typedef → struct key */
        char key[256];
        const char *bare_tn = tn;
        if (strncmp(bare_tn,"struct ",7)==0) bare_tn+=7;
        else if (strncmp(bare_tn,"union ",6)==0) bare_tn+=6;
        if (bare_tn == tn) {
            /* Could be a typedef */
            Symbol *td = symtable_lookup(cg->sym, tn);
            if (td && td->kind == SYM_TYPEDEF && td->type) {
                const char *tb = td->type->base;
                if (strncmp(tb,"struct ",7)==0) bare_tn = tb + 7;
                else if (strncmp(tb,"union ",6)==0) bare_tn = tb + 6;
                else bare_tn = tb;
            }
        }
        snprintf(key, sizeof key, "struct %s", bare_tn);
        Symbol *ss = symtable_lookup(cg->sym, key);
        if (!ss || !ss->struct_node) return 4;
        ASTNode *sd = ss->struct_node;
        for (int i = 0; i < sd->struct_decl.nfields; i++) {
            ASTNode *f = sd->struct_decl.fields[i];
            if (!f || f->kind != AST_FIELD) continue;
            if (f->field.name && strcmp(f->field.name, arr_expr->member.field)==0) {
                if (f->field.type) {
                    /* Array-of-pointers field (e.g. char *params[16]): element size is
                     * ptr_size, not sizeof(char).  Mirror the same special-case as Case 1. */
                    if (f->field.type->pointer_depth > 0 && f->field.array_size > 0)
                        return cg->is_64bit ? 8 : 4;
                    if (f->field.array_size2 > 0) {
                        /* T x[N][M] field: x[i] yields row i (M elements) —
                         * indexing it needs the row stride. */
                        int base_elem = typeinfo_size(f->field.type, cg->is_64bit);
                        if (base_elem < 1) base_elem = 1;
                        return base_elem * f->field.array_size2;
                    }
                    TypeInfo tmp;
                    tmp.base         = f->field.type->base;
                    tmp.pointer_depth= (f->field.type->pointer_depth>0) ? f->field.type->pointer_depth-1 : 0;
                    tmp.is_const     = f->field.type->is_const;
                    tmp.is_unsigned  = f->field.type->is_unsigned;
                    tmp.array_size   = -1;
                    tmp.pointed_to   = f->field.type->pointed_to;
                    tmp.is_volatile  = f->field.type->is_volatile;
                    tmp.is_inline    = f->field.type->is_inline;
                    tmp.is_extern    = f->field.type->is_extern;
                    tmp.is_float     = f->field.type->is_float;
                    /* Use sizeof_type_sym to handle typedef→struct */
                    return sizeof_type_sym(&tmp, cg->is_64bit, cg->sym);
                }
            }
        }
    }
    return 4;
}

/* A handful of standard-C-named CRT functions the real system
 * msvcrt.dll does NOT export under their plain name -- only under an
 * underscore-prefixed alias (confirmed via `dumpbin /exports msvcrt.dll`:
 * it has _strtoi64/_chmod, not strtoll/chmod). Any call that falls through
 * to the generic "unknown function -> msvcrt.dll" import path (see the two
 * "msvcrt.dll:%s" fallback sites below) must use the alias as BOTH the
 * import-table name and the asm_call_import label, or the loader fails the
 * whole process at startup with STATUS_ENTRYPOINT_NOT_FOUND before main()
 * ever runs. Signatures match exactly, so this is a safe drop-in rename. */
static const char *win_msvcrt_import_alias(const char *name) {
    if (strcmp(name,"strtoll")==0) return "_strtoi64";
    if (strcmp(name,"strtoull")==0) return "_strtoui64";
    if (strcmp(name,"chmod")==0) return "_chmod";
    return name;
}

/* =========================================================================
 * Internal Win32 function shims — malloc/free/memcpy etc backed by
 * HeapAlloc / HeapFree / kernel routines
 * ========================================================================= */
/* Avoid global pointer arrays (squash codegen can't initialize them).
 * Use explicit strcmp chain instead. */
int is_internal_shim(const char *name) {
    if (!name) return 0;
    return (strcmp(name,"ExitProcess")==0||
            strcmp(name,"malloc")==0||strcmp(name,"calloc")==0||strcmp(name,"realloc")==0||strcmp(name,"free")==0||
            strcmp(name,"memcpy")==0||strcmp(name,"memset")==0||strcmp(name,"memcmp")==0||strcmp(name,"memmove")==0||
            strcmp(name,"strlen")==0||strcmp(name,"strcpy")==0||strcmp(name,"strncpy")==0||strcmp(name,"strcmp")==0||
            strcmp(name,"strncmp")==0||strcmp(name,"strcat")==0||strcmp(name,"strncat")==0||
            strcmp(name,"strchr")==0||strcmp(name,"strrchr")==0||strcmp(name,"strstr")==0||
            strcmp(name,"strpbrk")==0||strcmp(name,"strtok")==0||
            strcmp(name,"_stricmp")==0||strcmp(name,"_strnicmp")==0||
            strcmp(name,"abs")==0||strcmp(name,"labs")==0||
            strcmp(name,"atoi")==0||strcmp(name,"atol")==0||strcmp(name,"atof")==0||
            strcmp(name,"strdup")==0||strcmp(name,"perror")==0||strcmp(name,"strerror")==0||
            strcmp(name,"getenv")==0||strcmp(name,"fprintf")==0||
            strcmp(name,"fopen")==0||strcmp(name,"fclose")==0||strcmp(name,"fgets")==0||
            strcmp(name,"fputs")==0||strcmp(name,"feof")==0||strcmp(name,"fflush")==0||
            strcmp(name,"fread")==0||strcmp(name,"fwrite")==0||strcmp(name,"fseek")==0||
            strcmp(name,"ftell")==0||strcmp(name,"rewind")==0||
            strcmp(name,"fgetc")==0||strcmp(name,"fputc")==0||strcmp(name,"ungetc")==0||
            strcmp(name,"remove")==0||strcmp(name,"rename")==0||strcmp(name,"ferror")==0||
            strcmp(name,"clearerr")==0||strcmp(name,"sprintf")==0||strcmp(name,"printf")==0||
            strcmp(name,"puts")==0||strcmp(name,"putchar")==0||strcmp(name,"abort")==0||
            strcmp(name,"exit")==0||strcmp(name,"vfprintf")==0||strcmp(name,"vprintf")==0||
            strcmp(name,"snprintf")==0||strcmp(name,"vsnprintf")==0);
}

/* Is `name` genuinely exported by one of the ".sqo" object files this build
 * is linking in (compiler.c populates cg->sqo_export_names before codegen
 * runs)? cg->prefer_static_calls only means "at least one .sqo is present"
 * — it says nothing about whether THIS bodyless call's name is among their
 * exports. A bodyless "extern"-declared function whose real definition is
 * an ordinary external library call (e.g. write(), by way of stdio.h's own
 * fprintf shim) is never going to show up here, no matter how many .sqo
 * files are linked — asm_call_static() would emit a RELOC_STATIC_REL32
 * that objfile_merge() can never resolve. See the CodeGen.sqo_export_names
 * comment (codegen.h) for how this was actually caught: squash self-
 * hosting itself for -macos. */
static int codegen_is_sqo_export(CodeGen *cg, const char *name) {
    for (int i=0;i<cg->sqo_export_count;i++)
        if (strcmp(cg->sqo_export_names[i], name)==0) return 1;
    return 0;
}

/* =========================================================================
 * Init
 * ========================================================================= */
void codegen_init(CodeGen *cg, Assembler *a, SymTable *sym, int is_64bit) {
    memset(cg,0,sizeof *cg);
    cg->asm_=a; cg->sym=sym; cg->is_64bit=is_64bit; cg->is_linux=a->is_linux;
    cg->loop_end_label=-1; cg->loop_top_label=-1; cg->switch_end_label=-1;
    cg->chkstk_lbl=-1;
    cg->string_cap=32; cg->strings=malloc(cg->string_cap*sizeof(StringEntry));
    cg->func_cap=32;   cg->funcs  =malloc(cg->func_cap  *sizeof(FuncRecord));
    cg->wdata_cap=32;  cg->wdata  =malloc(cg->wdata_cap *sizeof(WDataEntry));
    cg->float_const_cap=32; cg->float_consts=malloc(cg->float_const_cap*sizeof(StringEntry));
}

/* =========================================================================
 * String pool
 * ========================================================================= */
static const char *intern_string(CodeGen *cg, const char *value, int is_wide) {
    /* Wide (L"...") string literals must be encoded as real UTF-16LE data
     * (2 bytes per source char, plus a 2-byte null terminator) - squash
     * previously stored L"..." identically to a plain narrow "...", so any
     * code reading it via a wide-char-sized (WORD) load (SDL_wcslen and
     * friends, or squash's own [] indexing on a wchar_t pointer) combined
     * pairs of narrow bytes into garbage 16-bit values instead of reading
     * one genuine wide character per slot. This is a real, general squash
     * gap (any C using L"..." literals was affected, not an SDL bug) -
     * found while chasing an SDL_GetPrefPath "Out of memory" corruption
     * that traced back to SDL_sysfilesystem.c's SDL_wcslcat(path, L
     * backslash, ...) calls. */
    if (is_wide) {
        size_t nchars = strlen(value);
        int wlen = (int)((nchars+1)*2); /* +1 for the wide null terminator */
        char *wbuf = (char*)malloc((size_t)wlen);
        for (size_t i=0;i<nchars;i++) { wbuf[i*2]=value[i]; wbuf[i*2+1]=0; }
        wbuf[nchars*2]=0; wbuf[nchars*2+1]=0;
        /* Dedup via memcmp (wide data has embedded NUL bytes, so strcmp
         * would stop short and falsely "match" unrelated wide strings that
         * happen to share a leading narrow-looking prefix). */
        for (int i=0;i<cg->string_count;i++) {
            if (!cg->strings[i].is_wide || cg->strings[i].len != wlen) continue;
            if (memcmp(cg->strings[i].value, wbuf, (size_t)wlen)==0) { free(wbuf); return cg->strings[i].label; }
        }
        if (cg->string_count==cg->string_cap) {
            cg->string_cap*=2;
            cg->strings=realloc(cg->strings,cg->string_cap*sizeof(StringEntry));
        }
        StringEntry *se=&cg->strings[cg->string_count++];
        se->value   = wbuf;
        se->len     = wlen;
        se->is_wide = 1;
        se->offset  = cg->string_pool_size;
        cg->string_pool_size += se->len;
        char lbl[32]; snprintf(lbl,sizeof lbl,"str%d",cg->string_count-1);
        se->label  = my_strdup(lbl);
        return se->label;
    }
    for (int i=0;i<cg->string_count;i++) {
        if (cg->strings[i].len == 8) continue; /* skip float constant entries */
        if (cg->strings[i].is_wide) continue;
        if (strcmp(cg->strings[i].value,value)==0)
            return cg->strings[i].label;
    }
    if (cg->string_count==cg->string_cap) {
        cg->string_cap*=2;
        cg->strings=realloc(cg->strings,cg->string_cap*sizeof(StringEntry));
    }
    StringEntry *se=&cg->strings[cg->string_count++];
    se->value   = my_strdup(value);
    se->len     = (int)strlen(value)+1;
    se->is_wide = 0;
    se->offset = cg->string_pool_size;
    cg->string_pool_size += se->len;
    char lbl[32]; snprintf(lbl,sizeof lbl,"str%d",cg->string_count-1);
    se->label  = my_strdup(lbl);
    return se->label;
}

/* Allocate a zero-filled slot in the writable .data pool.
 * Returns the label name (pointer into WDataEntry.label). */
static const char *intern_wdata(CodeGen *cg, const char *label, int size) {
    /* Guard against NULL label (squash codegen may pass NULL for some struct-field
     * accesses); generate a unique fallback so the entry is still addressable. */
    char fallback[64];
    if (!label) {
        snprintf(fallback, sizeof fallback, "__wdata_anon_%d", cg->wdata_count);
        label = fallback;
    }
    if (cg->wdata_count == cg->wdata_cap) {
        cg->wdata_cap *= 2;
        if (cg->wdata_cap == 0) cg->wdata_cap = 32;
        cg->wdata = realloc(cg->wdata, cg->wdata_cap * sizeof(WDataEntry));
    }
    WDataEntry *we = &cg->wdata[cg->wdata_count];
    we->label   = my_strdup(label);
    we->offset  = cg->wdata_pool_size;
    we->size    = size;
    we->content = NULL;
    cg->wdata_count++;
    cg->wdata_pool_size += size;
    /* Align to 8 bytes */
    int pad = (8 - (cg->wdata_pool_size & 7)) & 7;
    cg->wdata_pool_size += pad;
    return we->label;
}

/* Same as intern_wdata(), but with real initial bytes instead of an
 * implicit zero-fill — `content` (exactly `size` bytes) is copied and
 * owned by the new entry. See WDataEntry.content's comment. */
static const char *intern_wdata_init(CodeGen *cg, const char *label, int size, const uint8_t *content) {
    const char *lbl = intern_wdata(cg, label, size);
    WDataEntry *we = &cg->wdata[cg->wdata_count-1];
    we->content = malloc((size_t)size);
    memcpy(we->content, content, (size_t)size);
    return lbl;
}

/* Evaluate a simple compile-time-constant scalar expression (a number, a
 * char literal, or a leading unary +/-/~ over one) to an integer value.
 * Returns 1 on success (value written to *out), 0 if the expression isn't
 * a constant this narrow evaluator understands. Used only to decide
 * whether a "static [const] T x = ...;" / "static [const] T x[] = {...};"
 * local's initializer can be baked directly into its .data storage at
 * compile time (no runtime init code needed — matching how real compilers
 * already treat this exact, extremely common case: a static lookup
 * table). */
static int const_expr_eval(ASTNode *e, long long *out) {
    if (!e) return 0;
    if (e->kind == AST_NUMBER) { *out = e->num.value; return 1; }
    if (e->kind == AST_CHAR_LIT) { *out = e->char_lit.value; return 1; }
    if (e->kind == AST_UNARY && !e->unary.post && e->unary.operand) {
        long long v;
        if (!const_expr_eval(e->unary.operand, &v)) return 0;
        if (strcmp(e->unary.op,"-")==0) { *out = -v; return 1; }
        if (strcmp(e->unary.op,"+")==0) { *out = v; return 1; }
        if (strcmp(e->unary.op,"~")==0) { *out = ~v; return 1; }
        return 0;
    }
    return 0;
}

/* Try to render a static local's initializer as real, pre-computed bytes:
 * `arr` little-endian `elem_sz`-wide elements if arr>0 (from a brace-list
 * initializer), else one `elem_sz`-wide scalar. Returns a malloc'd buffer
 * of `elem_sz * max(arr,1)` bytes on success (any brace-list elements
 * beyond what's understood are left zero, matching C's own "incomplete
 * initializer zero-fills the rest" rule), or NULL if the initializer isn't
 * a compile-time constant this understands — the caller then falls back
 * to the existing (correct, just less complete) zero-fill behavior. */
static uint8_t *build_static_local_init_bytes(ASTNode *init, int elem_sz, int arr) {
    if (!init || elem_sz <= 0) return NULL;
    int count = arr > 0 ? arr : 1;
    uint8_t *buf = calloc((size_t)count, (size_t)elem_sz);
    if (arr > 0) {
        if (init->kind != AST_BLOCK) { free(buf); return NULL; }
        int ne = init->block.count;
        if (ne > count) ne = count;
        for (int i=0;i<ne;i++) {
            long long v;
            if (!const_expr_eval(init->block.stmts[i], &v)) { free(buf); return NULL; }
            memcpy(buf + (size_t)i*elem_sz, &v, (size_t)elem_sz);
        }
    } else {
        long long v;
        if (!const_expr_eval(init, &v)) { free(buf); return NULL; }
        memcpy(buf, &v, (size_t)elem_sz);
    }
    return buf;
}

static uint8_t *build_rdata(CodeGen *cg, int *out_len) {
    /* Strings + float constants go into .rdata */
    int total = cg->string_pool_size;
    /* Align float constants to 8 bytes */
    int float_off = (total + 7) & ~7;
    total = float_off + cg->float_const_count * 8;
    if (total == 0) { *out_len=0; return NULL; }
    uint8_t *buf = calloc(total+8,1);
    for (int i=0;i<cg->string_count;i++)
        memcpy(buf+cg->strings[i].offset,cg->strings[i].value,cg->strings[i].len);
    /* Float constants after string pool */
    for (int i=0;i<cg->float_const_count;i++) {
        memcpy(buf+float_off+i*8, cg->float_consts[i].value, 8);
        cg->float_consts[i].offset = float_off + i*8;
    }
    *out_len = float_off + cg->float_const_count*8;
    return buf;
}

/* =========================================================================
 * Function label management
 * ========================================================================= */
static int get_func_label(CodeGen *cg, const char *name) {
    for (int i=0;i<cg->func_count;i++)
        if (strcmp(cg->funcs[i].name,name)==0) return cg->funcs[i].label_id;
    if (cg->func_count==cg->func_cap) {
        cg->func_cap*=2;
        cg->funcs=realloc(cg->funcs,cg->func_cap*sizeof(FuncRecord));
    }
    int id=asm_new_label(cg->asm_,name);
    cg->funcs[cg->func_count].name     =my_strdup(name);
    cg->funcs[cg->func_count].label_id =id;
    cg->func_count++;
    return id;
}

/* =========================================================================
 * sizeof helper
 * ========================================================================= */
/* Compute sizeof for a TypeInfo, looking up struct/typedef sizes from symtable */
static int sizeof_type_sym(TypeInfo *ti, int is_64, SymTable *sym) {
    if (!ti) return is_64 ? 8 : 4;
    if (ti->pointer_depth > 0) return is_64 ? 8 : 4;
    if (!ti->base) return 4;
    /* Array type: element_size * count */
    if (ti->array_size > 0) {
        TypeInfo tmp;
        tmp.base         = ti->base;
        tmp.pointer_depth= ti->pointer_depth;
        tmp.is_const     = ti->is_const;
        tmp.is_unsigned  = ti->is_unsigned;
        tmp.array_size   = 0;
        tmp.pointed_to   = ti->pointed_to;
        tmp.is_volatile  = ti->is_volatile;
        tmp.is_inline    = ti->is_inline;
        tmp.is_extern    = ti->is_extern;
        tmp.is_float     = ti->is_float;
        int esz = sizeof_type_sym(&tmp, is_64, sym);
        return esz * ti->array_size;
    }
    /* Try basic types first */
    int basic = typeinfo_size(ti, is_64);
    if (basic != 4) return basic; /* got a definitive size (char=1, short=2, etc.) */
    /* For structs, unions, typedefs — look up in symbol table */
    if (sym) {
        const char *b = ti->base;
        /* Direct struct/union key: "struct Foo" or "union Foo" */
        const char *bare = b;
        if (strncmp(bare,"struct ",7)==0) bare+=7;
        else if (strncmp(bare,"union ",6)==0) bare+=6;
        if (bare != b) {
            /* Is a struct/union type */
            char key[256]; snprintf(key,sizeof key,"struct %s",bare);
            Symbol *ss = symtable_lookup(sym, key);
            if (ss && ss->struct_size > 0) return ss->struct_size;
        }
        /* Try as typedef */
        Symbol *td = symtable_lookup(sym, b);
        if (td && td->kind==SYM_TYPEDEF && td->type) {
            /* Guard against "typedef enum Foo { ... } Foo;" (also common
             * for structs, but structs are already resolved above via
             * their "struct "-prefixed base and never reach here). An
             * enum's TypeInfo::base is just the bare tag name with no
             * "enum " prefix, so when the tag and the trailing typedef
             * alias are the same identifier (extremely common — this
             * exact idiom is used throughout SDL3, e.g. SDL_HintPriority),
             * td->type->base is textually identical to b, and recursing
             * calls sizeof_type_sym with an unchanged argument forever —
             * a genuine infinite recursion (confirmed via a real
             * STATUS_STACK_OVERFLOW). Enums are int-sized with no
             * separate tag registration to resolve further, so stop here
             * and fall through to returning `basic` instead of recursing. */
            if (!(td->type->base && strcmp(td->type->base, b) == 0)) {
                return sizeof_type_sym(td->type, is_64, sym);
            }
        }
        /* Try as struct key directly (e.g. "Foo" → "struct Foo") */
        char skey[256]; snprintf(skey,sizeof skey,"struct %s",b);
        Symbol *ss2 = symtable_lookup(sym, skey);
        if (ss2 && ss2->struct_size > 0) return ss2->struct_size;
    }
    return basic; /* default 4 for unknown types */
}
static int sizeof_type(TypeInfo *ti, int is_64) {
    return typeinfo_size(ti, is_64);
}

/* Returns the resolved size (in bytes) of the i-th parameter of func_sym.
 * Returns 0 if parameter type cannot be determined. */
static int param_type_sz(Symbol *func_sym, int i, SymTable *st, int is_64) {
    if (!func_sym || !func_sym->func_node) return 0;
    ASTNode *fn = func_sym->func_node;
    if (i >= fn->func.paramc) return 0;
    TypeInfo *pt = fn->func.params[i]->param.type;
    if (!pt) return 0;
    return sizeof_type_sym(pt, is_64, st);
}

/* Returns 1 if the i-th parameter of func_sym is a single-precision float (4 bytes, float type). */
static int param_is_single_float(Symbol *func_sym, int i, SymTable *st, int is_64) {
    if (!func_sym || !func_sym->func_node) return 0;
    ASTNode *fn = func_sym->func_node;
    if (i >= fn->func.paramc) return 0;
    TypeInfo *pt = fn->func.params[i]->param.type;
    if (!pt) return 0;
    /* Resolve typedef chain to get base type */
    int depth = 0;
    while (depth++ < 16) {
        if (typeinfo_is_float(pt)) {
            int sz = sizeof_type_sym(pt, is_64, st);
            return sz == 4; /* float=4, double=8 */
        }
        /* If base is a typedef name, resolve it */
        if (!pt->base || pt->pointer_depth > 0) return 0;
        Symbol *td = symtable_lookup(st, pt->base);
        if (!td || td->kind != SYM_TYPEDEF || !td->type) return 0;
        pt = td->type;
    }
    return 0;
}



/* =========================================================================
 * 32-bit cdecl long-long argument helpers
 * In 32-bit cdecl, a long long (8-byte) argument must occupy 8 bytes on the
 * stack (high word pushed first, low word pushed second / lower address).
 * These helpers detect and emit the 8-byte push for 32-bit mode only.
 * ========================================================================= */

/* Returns 1 if n is a 64-bit integer that needs 8 bytes in 32-bit cdecl. */
/* Find the TypeInfo of a named field inside a struct/union type (by type base name).
 * Recursively searches anonymous sub-structs/unions. Returns NULL if not found. */
static TypeInfo *lookup_field_typeinfo(SymTable *sym, const char *mstype, const char *fname) {
    const char *bare = mstype;
    if (strncmp(bare,"struct ",7)==0) bare+=7;
    else if (strncmp(bare,"union ",6)==0) bare+=6;
    char sk[256]; snprintf(sk,sizeof(sk),"struct %s",bare);
    Symbol *ss = symtable_lookup(sym, sk);
    if (!ss || !ss->struct_node) return NULL;
    ASTNode *sd = ss->struct_node;
    int fi;
    for (fi=0;fi<sd->struct_decl.nfields;fi++) {
        ASTNode *ff=sd->struct_decl.fields[fi];
        if (!ff||!ff->field.name) continue;
        if (strcmp(ff->field.name,fname)==0) return ff->field.type;
    }
    /* Search inside anonymous sub-structs/unions */
    for (fi=0;fi<sd->struct_decl.nfields;fi++) {
        ASTNode *ff=sd->struct_decl.fields[fi];
        if (!ff||!ff->field.name||strncmp(ff->field.name,"__anon_",7)!=0) continue;
        if (!ff->field.type||!ff->field.type->base) continue;
        TypeInfo *sub = lookup_field_typeinfo(sym, ff->field.type->base, fname);
        if (sub) return sub;
    }
    return NULL;
}

static int is_64bit_int_arg(CodeGen *cg, ASTNode *n) {
    int *w;
    if (!n || cg->is_64bit || codegen_is_float_expr(cg, n)) return 0;
    if (n->kind == AST_NUMBER) {
        w = (int*)&n->num.value;
        /* Needs 8 bytes if high word is not sign-extension of the low word */
        return w[1] != (w[0] < 0 ? -1 : 0);
    }
    if (n->kind == AST_VAR) {
        Symbol *s = symtable_lookup(cg->sym, n->var.name);
        if (s && s->type && typeinfo_size(s->type, 0) == 8 && !typeinfo_is_float(s->type))
            return 1;
    }
    if (n->kind == AST_MEMBER) {
        /* Look up the field's TypeInfo via the parent struct/union type */
        ASTNode *mobj = n->member.obj;
        const char *mfname = n->member.field;
        const char *mstype = NULL;
        if (mobj->kind == AST_VAR) {
            Symbol *sv = symtable_lookup(cg->sym, mobj->var.name);
            if (sv && sv->type) mstype = sv->type->base;
        } else if (mobj->kind == AST_DEREF || n->member.arrow) {
            ASTNode *op = (mobj->kind==AST_DEREF) ? mobj->deref.operand : mobj;
            if (op->kind == AST_VAR) {
                Symbol *sv2 = symtable_lookup(cg->sym, op->var.name);
                if (sv2 && sv2->type) mstype = sv2->type->base;
            }
        } else if (mobj->kind == AST_MEMBER || mobj->kind == AST_INDEX) {
            mstype = resolve_node_type(cg->sym, mobj);
        }
        if (mstype) {
            TypeInfo *ft = lookup_field_typeinfo(cg->sym, mstype, mfname);
            if (ft && typeinfo_size(ft, 0) == 8 && !typeinfo_is_float(ft))
                return 1;
        }
    }
    if (n->kind == AST_CAST && n->cast.type) {
        if (typeinfo_size(n->cast.type, 0) == 8 && !typeinfo_is_float(n->cast.type))
            return 1;
    }
    return 0;
}

/* Push a 64-bit integer argument in 32-bit cdecl: high word first, then low. */
static void push_64bit_int_arg(CodeGen *cg, ASTNode *n) {
    Assembler *a = cg->asm_;
    if (n->kind == AST_NUMBER) {
        int *w = (int*)&n->num.value;
        asm_push_imm32(a, w[1]);   /* push high word (higher stack address) */
        asm_push_imm32(a, w[0]);   /* push low  word (lower  stack address) */
    } else if (n->kind == AST_VAR) {
        Symbol *s = symtable_lookup(cg->sym, n->var.name);
        if (s && (s->kind == SYM_VAR || s->kind == SYM_PARAM)) {
            asm_mov_reg_mem(a, REG_EDX, REG_EBP, s->offset + 4); /* high word */
            asm_push_reg(a, REG_EDX);
            asm_mov_reg_mem(a, REG_EAX, REG_EBP, s->offset);     /* low  word */
            asm_push_reg(a, REG_EAX);
        } else {
            /* Global or unknown: evaluate to EAX and zero-extend high word */
            codegen_expr(cg, n);
            asm_xor_reg_reg(a, REG_EDX, REG_EDX);
            asm_push_reg(a, REG_EDX);
            asm_push_reg(a, REG_EAX);
        }
    } else if (n->kind == AST_MEMBER) {
        /* Get address of the 8-byte field via lvalue, then push hi+lo words */
        codegen_lvalue(cg, n);   /* EAX = address of the 64-bit field */
        asm_mov_reg_mem(a, REG_EDX, REG_EAX, 4); /* EDX = high word */
        asm_push_reg(a, REG_EDX);
        asm_mov_reg_mem(a, REG_EAX, REG_EAX, 0); /* EAX = low  word */
        asm_push_reg(a, REG_EAX);
    } else {
        /* Generic: evaluate to EAX and zero-extend high word */
        codegen_expr(cg, n);
        asm_xor_reg_reg(a, REG_EDX, REG_EDX);
        asm_push_reg(a, REG_EDX);
        asm_push_reg(a, REG_EAX);
    }
}

/* =========================================================================
 * Linux libc.so.6 direct call helper (SysV AMD64 / i386 cdecl)
 * Evaluates args left-to-right into a scratch region on the stack,
 * then loads them into SysV regs, then calls the import via GOT.
 * ========================================================================= */
static void emit_linux_libc_call(CodeGen *cg, const char *fname, ASTNode **args, int argc) {
    Assembler *a = cg->asm_;
    char key[128]; snprintf(key, sizeof key, "%s:%s", g_squash_libc_soname, fname);
    symtable_add_import(cg->sym, key);
    if (cg->is_64bit) {
        int nreg = argc < 6 ? argc : 6;
        int nextra = argc > 6 ? argc - 6 : 0;
        /* frame must be a multiple of 16 so RSP = 0 mod 16 before CALL (SysV AMD64) */
        int frame = (argc > 0 ? argc : 1) * 8;
        frame = (frame + 15) & ~15;
        if (frame < 16) frame = 16;
        asm_sub_rsp(a, frame);
        /* scratch region starts at [rsp + nextra*8] */
        int scratch = nextra * 8;
        int arg_is_float_l[6] = {0,0,0,0,0,0};
        int i = 0;
        while (i < nreg) {
            int af = codegen_is_float_expr(cg, args[i]);
            arg_is_float_l[i] = af;
            if (af) { codegen_float_expr(cg, args[i]); asm_movsd_store(a, REG_RSP, scratch+i*8, 0); }
            else    { codegen_expr(cg, args[i]); asm_mov_mem_reg(a, REG_RSP, scratch + i*8, REG_RAX); }
            i++;
        }
        i = 0;
        while (i < nextra) {
            /* See the identical fix (and its longer comment) on the direct-
             * call SysV path in the AST_CALL codegen -- same bug, same fix,
             * for this libc-import call path (e.g. an fprintf/snprintf
             * call with more than 6 total arguments, one of the 7th+ being
             * a float/double). */
            int af = codegen_is_float_expr(cg, args[6+i]);
            if (af) { codegen_float_expr(cg, args[6+i]); asm_movsd_store(a, REG_RSP, i*8, 0); }
            else    { codegen_expr(cg, args[6+i]); asm_mov_mem_reg(a, REG_RSP, i*8, REG_RAX); }
            i++;
        }
        {
#define LIBC_IREG(i) ((i)==0?REG_RDI:(i)==1?REG_RSI:(i)==2?REG_RDX:(i)==3?REG_RCX:(i)==4?REG_R8:REG_R9)
            int ireg = 0, freg = 0, _k;
            for (_k = 0; _k < nreg; _k++) {
                if (arg_is_float_l[_k]) {
                    asm_movsd_load(a, freg, REG_RSP, scratch + _k*8);
                    freg++;
                } else {
                    asm_mov_reg_mem(a, LIBC_IREG(ireg), REG_RSP, scratch + _k*8);
                    ireg++;
                }
            }
#undef LIBC_IREG
            /* SysV AMD64 ABI: a variadic callee (printf/fprintf/snprintf/
             * sprintf, all routed through this function) reads AL as the
             * number of vector (XMM) registers used for the call, and
             * uses it to decide how many of XMM0-7 to spill into its own
             * register-save area for va_arg -- skipping that spill for
             * any register AL says wasn't used. Leaving AL as whatever
             * happened to be in RAX before this call (e.g. a previous
             * call's return value, or leftover from evaluating an
             * argument expression) means a callee that DOES receive a
             * float/double vararg can find its value never saved,
             * reading garbage back via va_arg — and, conversely, a
             * caller that legitimately passes zero float varargs still
             * needs AL=0 set explicitly, not merely "whatever's already
             * there", since that value is never otherwise guaranteed to
             * be zero. Must be set every call, not just when freg>0. */
            asm_mov_reg_imm(a, REG_RAX, freg);
        }
        asm_call_import(a, fname);
        asm_add_rsp(a, frame);
    } else {
        /* 32-bit: cdecl, push right-to-left */
        int total = 0;
        int i = argc - 1;
        while (i >= 0) {
            int af = codegen_is_float_expr(cg, args[i]);
            if (af) {
                codegen_float_expr(cg, args[i]);
                asm_sub_rsp(a, 8);
                asm_fstp_mem64(a, REG_ESP, 0);
                total += 8;
            } else if (is_64bit_int_arg(cg, args[i])) {
                push_64bit_int_arg(cg, args[i]);
                total += 8;
            } else {
                codegen_expr(cg, args[i]);
                asm_push_reg(a, REG_EAX);
                total += 4;
            }
            i--;
        }
        asm_call_import32(a, fname);
        if (total > 0) asm_add_rsp(a, total);
    }
}

/* =========================================================================
 * Internal shim dispatch
 * ========================================================================= */
static void emit_internal_call(CodeGen *cg, const char *name, ASTNode **args, int argc) {
    Assembler *a = cg->asm_;

    /* -----------------------------------------------------------------------
     * Linux: route all shims directly to libc.so.6 via SysV calling convention
     * --------------------------------------------------------------------- */
    if (cg->is_linux) {
        /* ExitProcess(code) -> libc exit(code) on Linux */
        if (strcmp(name,"ExitProcess")==0) {
            emit_linux_libc_call(cg, "exit", args, argc);
            return;
        }
        /* malloc/calloc/realloc/free/exit/abort: libc direct */
        if (strcmp(name,"malloc")==0||strcmp(name,"calloc")==0||
            strcmp(name,"realloc")==0||strcmp(name,"free")==0) {
            emit_linux_libc_call(cg, name, args, argc);
            return;
        }
        if (strcmp(name,"exit")==0||strcmp(name,"abort")==0) {
            /* For abort, call exit(1) */
            if (strcmp(name,"abort")==0) {
                ASTNode *dummy[1];
                emit_linux_libc_call(cg, "exit", dummy, 0);
                /* unreachable, but emit syscall exit(1) for safety */
                asm_mov_reg_imm(a, REG_RAX, 0);
            } else {
                if (argc >= 1) codegen_expr(cg, args[0]);
                else asm_mov_reg_imm(a, REG_RAX, 0);
                if (cg->is_64bit) {
                    asm_mov_reg_reg(a, REG_RDI, REG_RAX);
                    asm_mov_reg_imm(a, REG_RAX, 60); /* sys_exit */
                    asm_emit2(a, 0x0F, 0x05); /* syscall */
                } else {
                    asm_mov_reg_reg(a, REG_EBX, REG_EAX);
                    asm_mov_reg_imm(a, REG_EAX, 1); /* sys_exit */
                    asm_emit2(a, 0xCD, 0x80); /* int 0x80 */
                }
            }
            return;
        }
        /* memset/memcpy/memmove/memcmp/strlen/strcpy/strcmp/strncmp/strcat: inline shims work */
        if (strcmp(name,"memset")==0||strcmp(name,"memcpy")==0||strcmp(name,"memmove")==0||
            strcmp(name,"memcmp")==0||strcmp(name,"strlen")==0||strcmp(name,"strcpy")==0||
            strcmp(name,"strcmp")==0||strcmp(name,"strncmp")==0||strcmp(name,"strcat")==0) {
            /* fall through to the inline implementations below */
        } else {
            /* All other stdlib functions: direct libc.so.6 call.
             * _stricmp/_strnicmp are MSVCRT-only symbol names (see
             * include/string.h's own "Windows-specific" comment) -- real
             * glibc.so exports no symbol by either name, so calling
             * through unchanged always failed at runtime with "undefined
             * symbol" on any -linux build that used them (confirmed via
             * SQW/html_lexer.c, the first real -linux caller of
             * _strnicmp -- see Makefile.SQW.linux). Route to the POSIX
             * equivalents instead (strcasecmp/strncasecmp, same argument
             * order/count), which glibc genuinely exports. */
            const char *linux_name = name;
            if (strcmp(name,"_stricmp")==0) linux_name = "strcasecmp";
            else if (strcmp(name,"_strnicmp")==0) linux_name = "strncasecmp";
            emit_linux_libc_call(cg, linux_name, args, argc);
            return;
        }
    }

    /* -----------------------------------------------------------------------
     * printf(fmt, ...) / puts(str)
     *
     * Supported format specifiers: %d %i %u %x %X %c %s %%
     * Strategy: split the format string at compile time into literal chunks
     * and integer/string slots, then emit code to:
     *   1. Write each literal chunk via WriteFile.
     *   2. For %d/%i/%u/%x: convert the integer arg to decimal/hex digits
     *      using an inline itoa loop and write the result.
     *   3. For %s: write the string argument via WriteFile.
     *   4. For %c: write a 1-byte buffer containing the char.
     * This requires no libc and works entirely through WriteFile.
     * --------------------------------------------------------------------- */
    if (strcmp(name,"printf")==0 || strcmp(name,"puts")==0) {
        /* Call printf/puts directly from msvcrt.dll - clean, no shellcode patterns */
        const char *fn = (strcmp(name,"puts")==0) ? "puts" : "printf";
        symtable_add_import(cg->sym,"msvcrt.dll:printf");
        symtable_add_import(cg->sym,"msvcrt.dll:puts");
        /* Fall through to normal function call handling below */
        (void)fn;
        /* Re-route: treat as normal external call to msvcrt */
        {
            char key[64]; snprintf(key,sizeof key,"msvcrt.dll:%s",name);
            symtable_add_import(cg->sym,key);
            if (cg->is_64bit) {
                /* Eval args into RCX,RDX,R8,R9 FIRST (no shadow yet - no red zone).
                 * Save already-set regs if subsequent arg evaluation clobbers them
                 * (e.g. strlen shim clears RCX; we save to R10/R11/R12/R13). */
                /* Avoid static arrays (squash can't init non-zero local statics).
                 * Use helper macros for arg regs and save regs. */
#define ARG_REG(i)  ((i)==0?REG_RCX:(i)==1?REG_RDX:(i)==2?REG_R8:REG_R9)
#define SAVE_REG(k) ((k)==0?REG_R10:(k)==1?REG_R11:(k)==2?REG_R12:REG_R13)
                for (int i=0;i<argc&&i<4;i++) {
                    /* Before evaluating arg[i], save already-placed args 0..i-1
                     * if this arg's evaluation might clobber them (has inner call). */
                    int this_has_call = expr_has_call(args[i]);
                    if (this_has_call) {
                        for (int k=0; k<i; k++)
                            asm_mov_reg_reg(a, SAVE_REG(k), ARG_REG(k));
                    }
                    int af=codegen_is_float_expr(cg,args[i]);
                    if(af) {
                        /* codegen_float_expr() always computes into XMM0 — move
                         * to this argument's own XMMi home first (matters once
                         * there's more than one float/double vararg in the same
                         * printf call), THEN duplicate the raw bits into the
                         * matching integer register: printf/vsnprintf are
                         * variadic, so Win64's ABI requires every "..." slot's
                         * value to also be present in its integer register —
                         * the callee has no compile-time type for that slot and
                         * always reads the shadow-space copy via RCX/RDX/R8/R9,
                         * never XMM. Without this, every %f-style argument
                         * silently read as whatever garbage was already in that
                         * register (confirmed: even "printf(\"%f\",1.5)" printed
                         * 0.000000). */
                        codegen_float_expr(cg,args[i]);
                        if (i != 0) asm_movsd_xmm(a, i, 0);
                        asm_movq_gpr_from_xmm(a, ARG_REG(i), i);
                    }
                    else { codegen_expr(cg,args[i]); asm_mov_reg_reg(a,ARG_REG(i),REG_RAX); }
                    if (this_has_call) {
                        for (int k=0; k<i; k++)
                            asm_mov_reg_reg(a, ARG_REG(k), SAVE_REG(k));
                    }
                }
#undef ARG_REG
#undef SAVE_REG
                /* Extra args on stack (args 4+): need shadow first.
                 * Protect RCX/RDX/R8/R9 (already holding args 0..3) if a
                 * stack arg's own evaluation contains a nested call —
                 * same fix as the snprintf/fprintf shims. */
                int extra=(argc>4)?(argc-4)*8:0;
                int frame=32+extra; if((frame&8)==0) frame+=8;
                asm_sub_rsp(a,frame);
                for(int i=4;i<argc;i++){
                    int this_has_call = expr_has_call(args[i]);
                    if (this_has_call) {
                        asm_mov_reg_reg(a,REG_R10,REG_RCX); asm_mov_reg_reg(a,REG_R11,REG_RDX);
                        asm_mov_reg_reg(a,REG_R12,REG_R8);  asm_mov_reg_reg(a,REG_R13,REG_R9);
                    }
                    int af=codegen_is_float_expr(cg,args[i]);
                    if(af){codegen_float_expr(cg,args[i]);asm_movsd_store(a,REG_RSP,32+(i-4)*8,0);}
                    else{codegen_expr(cg,args[i]);asm_mov_mem_reg(a,REG_RSP,32+(i-4)*8,REG_RAX);}
                    if (this_has_call) {
                        asm_mov_reg_reg(a,REG_RCX,REG_R10); asm_mov_reg_reg(a,REG_RDX,REG_R11);
                        asm_mov_reg_reg(a,REG_R8,REG_R12);  asm_mov_reg_reg(a,REG_R9,REG_R13);
                    }
                }
                asm_call_import(a,name);
                asm_add_rsp(a,frame);
            } else {
                /* 32-bit cdecl: push args right-to-left */
                int total=0;
                for(int i=argc-1;i>=0;i--){
                    int af=codegen_is_float_expr(cg,args[i]);
                    if(af){codegen_float_expr(cg,args[i]);asm_sub_rsp(a,8);asm_fstp_mem64(a,REG_ESP,0);total+=8;}
                    else if(is_64bit_int_arg(cg,args[i])){push_64bit_int_arg(cg,args[i]);total+=8;}
                    else{codegen_expr(cg,args[i]);asm_push_reg(a,REG_EAX);total+=4;}
                }
                asm_call_import32(a,name);
                if(total>0) asm_add_rsp(a,total);
            }
        }
        return;

    }

    /* -----------------------------------------------------------------------
     * malloc(size) — HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY=8, size)
     * Returns pointer in RAX/EAX.
     * --------------------------------------------------------------------- */
    if (strcmp(name,"malloc")==0) {
        symtable_add_import(cg->sym,"KERNEL32.dll:HeapAlloc");
        symtable_add_import(cg->sym,"KERNEL32.dll:GetProcessHeap");
        /* Evaluate size argument first, save it */
        if (argc >= 1) codegen_expr(cg, args[0]);
        else           asm_mov_reg_imm(a, REG_RAX, 0);

        if (cg->is_64bit) {
            /* size is in RAX — move to R8 (arg3), then get heap handle */
            asm_push_reg(a, REG_RAX);           /* save size on stack        */
            asm_sub_rsp(a, 40);
            asm_call_import(a, "GetProcessHeap");
            asm_add_rsp(a, 40);
            /* RAX = heap handle, RCX = handle, RDX = 8 (ZERO), R8 = size */
            asm_mov_reg_reg(a, REG_RCX, REG_RAX);
            asm_mov_reg_imm(a, REG_RDX, 0);    /* HeapAlloc flags = 0       */
            asm_pop_reg(a, REG_R8);             /* pop size → R8             */
            asm_sub_rsp(a, 40);
            asm_call_import(a, "HeapAlloc");
            asm_add_rsp(a, 40);
        } else {
            /* 32-bit stdcall: push size, flags, handle (right-to-left) */
            asm_push_reg(a, REG_EAX);           /* save size                 */
            asm_push_imm32(a, 0);               /* placeholder for handle    */
            asm_call_import32(a, "GetProcessHeap");
            /* EAX = heap handle; fix up the placeholder on stack           */
            /* [ESP+0] = placeholder we just pushed; EAX = real handle      */
            /* Re-push in correct order: handle, flags=8, size              */
            asm_add_rsp(a, 4);                  /* discard placeholder (keep EBX clean) */
            asm_pop_reg(a, REG_ECX);            /* ECX = size                */
            asm_push_reg(a, REG_ECX);           /* push size                 */
            asm_push_imm32(a, 0);               /* push HeapAlloc flags=0    */
            asm_push_reg(a, REG_EAX);           /* push handle               */
            asm_call_import32(a, "HeapAlloc");
        }
        return;
    }

    /* -----------------------------------------------------------------------
     * calloc(count, size) — same as malloc(count*size) but always zeroed
     * HeapAlloc(flags=0); caller is responsible for zeroing if needed.
     * --------------------------------------------------------------------- */
    if (strcmp(name,"calloc")==0) {
        symtable_add_import(cg->sym,"KERNEL32.dll:HeapAlloc");
        symtable_add_import(cg->sym,"KERNEL32.dll:GetProcessHeap");
        /* compute count * elemsize */
        if (argc >= 2) {
            codegen_expr(cg, args[0]);          /* count in RAX              */
            asm_push_reg(a, REG_RAX);
            codegen_expr(cg, args[1]);          /* size  in RAX              */
            asm_pop_reg(a, REG_RCX);            /* ECX/RCX = count (scratch, not callee-saved) */
            asm_imul_reg_reg(a, REG_RAX, REG_RCX); /* RAX = count*size      */
        } else if (argc >= 1) {
            codegen_expr(cg, args[0]);
        } else {
            asm_mov_reg_imm(a, REG_RAX, 0);
        }

        if (cg->is_64bit) {
            asm_push_reg(a, REG_RAX);
            asm_sub_rsp(a, 40);
            asm_call_import(a, "GetProcessHeap");
            asm_add_rsp(a, 40);
            asm_mov_reg_reg(a, REG_RCX, REG_RAX);
            asm_mov_reg_imm(a, REG_RDX, 8); /* HEAP_ZERO_MEMORY */
            asm_pop_reg(a, REG_R8);
            asm_sub_rsp(a, 40);
            asm_call_import(a, "HeapAlloc");
            asm_add_rsp(a, 40);
        } else {
            asm_push_reg(a, REG_EAX);
            asm_push_imm32(a, 0);
            asm_call_import32(a, "GetProcessHeap");
            asm_add_rsp(a, 4);                  /* discard placeholder (keep EBX clean) */
            asm_pop_reg(a, REG_ECX);
            asm_push_reg(a, REG_ECX);
            asm_push_imm32(a, 8); /* HEAP_ZERO_MEMORY */
            asm_push_reg(a, REG_EAX);
            asm_call_import32(a, "HeapAlloc");
        }
        return;
    }

    /* -----------------------------------------------------------------------
     * realloc(ptr, size) — HeapReAlloc(GetProcessHeap(), 0, ptr, size)
     * If ptr is NULL, falls back to HeapAlloc (matches C standard).
     * --------------------------------------------------------------------- */
    if (strcmp(name,"realloc")==0) {
        symtable_add_import(cg->sym,"KERNEL32.dll:HeapReAlloc");
        symtable_add_import(cg->sym,"KERNEL32.dll:HeapAlloc");
        symtable_add_import(cg->sym,"KERNEL32.dll:GetProcessHeap");
        /* Evaluate ptr (arg0) and size (arg1) */
        if (argc >= 1) codegen_expr(cg, args[0]);
        else           asm_mov_reg_imm(a, REG_RAX, 0);

        if (cg->is_64bit) {
            asm_push_reg(a, REG_RAX);               /* save ptr */
            if (argc >= 2) codegen_expr(cg, args[1]);
            else           asm_mov_reg_imm(a, REG_RAX, 0);
            asm_push_reg(a, REG_RAX);               /* save size */
            asm_sub_rsp(a, 40);
            asm_call_import(a, "GetProcessHeap");
            asm_add_rsp(a, 40);
            /* RAX = heap, stack: [size, ptr] */
            asm_mov_reg_reg(a, REG_RCX, REG_RAX);  /* RCX = heap */
            asm_mov_reg_imm(a, REG_RDX, 0);         /* RDX = flags = 0 */
            asm_pop_reg(a, REG_R9);                 /* R9 = size */
            asm_pop_reg(a, REG_R8);                 /* R8 = ptr */
            /* If ptr==NULL use HeapAlloc instead */
            int use_alloc = asm_new_label(a, "realloc_alloc");
            int done_lbl  = asm_new_label(a, "realloc_done");
            /* test r8,r8 */
            asm_emit3(a, 0x4D, 0x85, 0xC0);
            asm_jcc_label(a, CC_E, use_alloc);
            /* HeapReAlloc(heap, 0, ptr, size): RCX=heap RDX=0 R8=ptr R9=size */
            asm_sub_rsp(a, 40);
            asm_call_import(a, "HeapReAlloc");
            asm_add_rsp(a, 40);
            asm_jmp_label(a, done_lbl);
            asm_def_label(a, use_alloc);
            /* HeapAlloc(heap, 0, size): RCX=heap RDX=0 R8=size */
            asm_mov_reg_reg(a, REG_R8, REG_R9);
            asm_sub_rsp(a, 40);
            asm_call_import(a, "HeapAlloc");
            asm_add_rsp(a, 40);
            asm_def_label(a, done_lbl);
        } else {
            /* 32-bit: realloc(ptr,size) → HeapAlloc if ptr==NULL, else HeapReAlloc */
            asm_push_reg(a, REG_EAX);               /* save ptr */
            if (argc >= 2) codegen_expr(cg, args[1]);
            else           asm_mov_reg_imm(a, REG_RAX, 0);
            asm_push_reg(a, REG_EAX);               /* save size */
            asm_push_imm32(a, 0);                   /* placeholder for GetProcessHeap call */
            asm_call_import32(a, "GetProcessHeap");
            asm_add_rsp(a, 4);                      /* discard placeholder */
            /* stack: [size, ptr], EAX = heap */
            asm_pop_reg(a, REG_ECX);                /* ECX = size */
            asm_pop_reg(a, REG_EDX);                /* EDX = ptr */
            /* if ptr==NULL use HeapAlloc (HeapReAlloc crashes on NULL ptr) */
            int _ra32_alloc = asm_new_label(a, "realloc_alloc32");
            int _ra32_done  = asm_new_label(a, "realloc_done32");
            asm_emit2(a, 0x85, 0xD2);               /* test edx,edx */
            asm_jcc_label(a, CC_E, _ra32_alloc);
            /* HeapReAlloc(heap, 0, ptr, size) — ptr is non-NULL */
            asm_push_reg(a, REG_ECX);               /* push size */
            asm_push_reg(a, REG_EDX);               /* push ptr */
            asm_push_imm32(a, 0);                   /* push flags=0 */
            asm_push_reg(a, REG_EAX);               /* push heap */
            asm_call_import32(a, "HeapReAlloc");
            asm_jmp_label(a, _ra32_done);
            asm_def_label(a, _ra32_alloc);
            /* HeapAlloc(heap, 0, size) — ptr was NULL */
            asm_push_reg(a, REG_ECX);               /* push size */
            asm_push_imm32(a, 0);                   /* push flags=0 */
            asm_push_reg(a, REG_EAX);               /* push heap */
            asm_call_import32(a, "HeapAlloc");
            asm_def_label(a, _ra32_done);
        }
        return;
    }

    /* -----------------------------------------------------------------------
     * free(ptr) — HeapFree(GetProcessHeap(), 0, ptr)
     * --------------------------------------------------------------------- */
    if (strcmp(name,"free")==0) {
        symtable_add_import(cg->sym,"KERNEL32.dll:HeapFree");
        symtable_add_import(cg->sym,"KERNEL32.dll:GetProcessHeap");
        if (argc >= 1) codegen_expr(cg, args[0]);
        else           asm_mov_reg_imm(a, REG_RAX, 0);

        if (cg->is_64bit) {
            asm_push_reg(a, REG_RAX);           /* save ptr (arg3)           */
            asm_sub_rsp(a, 40);
            asm_call_import(a, "GetProcessHeap");
            asm_add_rsp(a, 40);
            asm_mov_reg_reg(a, REG_RCX, REG_RAX); /* RCX = heap             */
            asm_mov_reg_imm(a, REG_RDX, 0);     /* RDX = dwFlags = 0        */
            asm_pop_reg(a, REG_R8);             /* R8 = ptr                  */
            asm_sub_rsp(a, 40);
            asm_call_import(a, "HeapFree");
            asm_add_rsp(a, 40);
        } else {
            /* stdcall: HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem) */
            asm_push_reg(a, REG_EAX);           /* save ptr                  */
            asm_push_imm32(a, 0);               /* placeholder               */
            asm_call_import32(a, "GetProcessHeap");
            asm_add_rsp(a, 4);                  /* discard placeholder (keep EBX clean) */
            asm_pop_reg(a, REG_ECX);            /* ECX = ptr                 */
            asm_push_reg(a, REG_ECX);           /* push ptr (lpMem)          */
            asm_push_imm32(a, 0);               /* push flags=0              */
            asm_push_reg(a, REG_EAX);           /* push handle               */
            asm_call_import32(a, "HeapFree");
        }
        asm_mov_reg_imm(a, REG_RAX, 0);
        return;
    }

    /* -----------------------------------------------------------------------
     * memset(dst, val, count) — rep stosb
     * --------------------------------------------------------------------- */
    if (strcmp(name,"memset")==0) {
        if (argc >= 3) {
            codegen_expr(cg, args[0]); asm_push_reg(a, REG_RAX); /* dst    */
            codegen_expr(cg, args[2]); asm_push_reg(a, REG_RAX); /* count  */
            codegen_expr(cg, args[1]);                            /* val    */
            if (cg->is_64bit) {
                asm_pop_reg(a, REG_RCX);  /* count */
                asm_pop_reg(a, REG_RDI);  /* dst   */
                /* AL = val (low byte of RAX), RCX = count, RDI = dst */
                asm_emit1(a, 0xF3); asm_emit1(a, 0xAA); /* rep stosb */
                asm_mov_reg_reg(a, REG_RAX, REG_RDI);
            } else {
                asm_pop_reg(a, REG_ECX);
                asm_pop_reg(a, REG_EDI);
                asm_emit1(a, 0xF3); asm_emit1(a, 0xAA);
                asm_mov_reg_reg(a, REG_EAX, REG_EDI);
            }
        } else {
            asm_mov_reg_imm(a, REG_RAX, 0);
        }
        return;
    }

    /* -----------------------------------------------------------------------
     * memcpy(dst, src, count) — rep movsb
     * --------------------------------------------------------------------- */
    if (strcmp(name,"memcpy")==0 || strcmp(name,"memmove")==0) {
        if (argc >= 3) {
            codegen_expr(cg, args[0]); asm_push_reg(a, REG_RAX); /* dst    */
            codegen_expr(cg, args[1]); asm_push_reg(a, REG_RAX); /* src    */
            codegen_expr(cg, args[2]);                            /* count  */
            if (cg->is_64bit) {
                asm_mov_reg_reg(a, REG_RCX, REG_RAX);
                asm_pop_reg(a, REG_RSI);
                asm_pop_reg(a, REG_RDI);
                asm_emit1(a, 0xF3); asm_emit1(a, 0xA4); /* rep movsb */
            } else {
                asm_mov_reg_reg(a, REG_ECX, REG_EAX);
                asm_pop_reg(a, REG_ESI);
                asm_pop_reg(a, REG_EDI);
                asm_emit1(a, 0xF3); asm_emit1(a, 0xA4);
            }
        }
        asm_mov_reg_imm(a, REG_RAX, 0);
        return;
    }

    /* -----------------------------------------------------------------------
     * memcmp(a, b, n) — byte-by-byte comparison, returns signed result
     * --------------------------------------------------------------------- */
    if (strcmp(name,"memcmp")==0 && argc>=3) {
        if (cg->is_64bit) {
            codegen_expr(cg,args[2]); asm_mov_reg_reg(a,REG_RCX,REG_RAX);
            codegen_expr(cg,args[0]); asm_mov_reg_reg(a,REG_RSI,REG_RAX);
            codegen_expr(cg,args[1]); asm_mov_reg_reg(a,REG_RDI,REG_RAX);
            int lp=asm_new_label(a,"mc_lp"),dn=asm_new_label(a,"mc_dn");
            asm_def_label(a,lp);
            asm_emit3(a,0x48,0x85,0xC9); asm_jcc_label(a,CC_E,dn); /* test rcx,rcx */
            asm_emit3(a,0x0F,0xB6,0x06); /* movzx eax,byte[rsi] */
            asm_emit3(a,0x0F,0xB6,0x1F); /* movzx ebx,byte[rdi] */
            asm_emit2(a,0x38,0xD8);       /* cmp al,bl */
            asm_jcc_label(a,CC_NE,dn);
            asm_emit3(a,0x48,0xFF,0xC6); asm_emit3(a,0x48,0xFF,0xC7); asm_emit3(a,0x48,0xFF,0xC9);
            asm_jmp_label(a,lp);
            asm_def_label(a,dn);
            asm_emit2(a,0x29,0xD8);       /* sub eax,ebx */
            asm_emit3(a,0x48,0x63,0xC0); /* movsxd rax,eax */
        } else {
            codegen_expr(cg,args[2]); asm_emit2(a,0x89,0xC1);
            codegen_expr(cg,args[0]); asm_emit2(a,0x89,0xC6);
            codegen_expr(cg,args[1]); asm_emit2(a,0x89,0xC7);
            int lp=asm_new_label(a,"mc_lp"),dn=asm_new_label(a,"mc_dn");
            asm_def_label(a,lp);
            asm_emit2(a,0x85,0xC9); asm_jcc_label(a,CC_E,dn);
            asm_emit3(a,0x0F,0xB6,0x06); asm_emit3(a,0x0F,0xB6,0x1F);
            asm_emit2(a,0x38,0xD8); asm_jcc_label(a,CC_NE,dn);
            asm_emit2(a,0xFF,0xC6); asm_emit2(a,0xFF,0xC7); asm_emit2(a,0xFF,0xC9);
            asm_jmp_label(a,lp);
            asm_def_label(a,dn);
            asm_emit2(a,0x29,0xD8); /* sub eax,ebx */
        }
        return;
    }

    /* -----------------------------------------------------------------------
     * strlen(str) — count bytes until NUL byte
     * --------------------------------------------------------------------- */
    if (strcmp(name,"strlen")==0) {
        if (argc >= 1) {
            codegen_expr(cg, args[0]);  /* ptr in RAX */
            if (cg->is_64bit) {
                /* xor rcx,rcx; loop: cmp byte[rax+rcx],0; je done; inc rcx; jmp */
                asm_emit3(a, 0x48, 0x31, 0xC9);       /* xor rcx,rcx          */
                int lp = asm_new_label(a, "strlen_lp");
                asm_def_label(a, lp);
                asm_emit4(a, 0x80, 0x3C, 0x08, 0x00); /* cmp [rax+rcx],0      */
                int dn = asm_new_label(a, "strlen_dn");
                asm_jcc_label(a, CC_E, dn);
                asm_emit3(a, 0x48, 0xFF, 0xC1);        /* inc rcx              */
                asm_jmp_label(a, lp);
                asm_def_label(a, dn);
                asm_mov_reg_reg(a, REG_RAX, REG_RCX);
            } else {
                asm_emit2(a, 0x31, 0xC9);
                int lp = asm_new_label(a, "strlen_lp");
                asm_def_label(a, lp);
                asm_emit3(a, 0x80, 0x3C, 0x08); asm_emit1(a, 0x00);
                int dn = asm_new_label(a, "strlen_dn");
                asm_jcc_label(a, CC_E, dn);
                asm_emit1(a, 0x41);
                asm_jmp_label(a, lp);
                asm_def_label(a, dn);
                asm_mov_reg_reg(a, REG_EAX, REG_ECX);
            }
        } else {
            asm_mov_reg_imm(a, REG_RAX, 0);
        }
        return;
    }

    /* ---- strcpy(dst, src) -> dst ---- */
    if (strcmp(name,"strcpy")==0 && argc>=2) {
        /* Use REP MOVSB: measure src length, then block copy including NUL */
        if (cg->is_64bit) {
            codegen_expr(cg,args[0]); asm_mov_reg_reg(a,REG_RDI,REG_RAX);
            codegen_expr(cg,args[1]); asm_mov_reg_reg(a,REG_RSI,REG_RAX);
            /* measure src length into RCX */
            asm_mov_reg_reg(a,REG_RCX,REG_RSI);
            int lp=asm_new_label(a,"sc_lp"),dn=asm_new_label(a,"sc_dn");
            asm_def_label(a,lp);
            asm_emit3(a,0x80,0x39,0x00);   /* cmp byte[rcx],0 */
            asm_jcc_label(a,CC_E,dn);
            asm_emit3(a,0x48,0xFF,0xC1);   /* inc rcx */
            asm_jmp_label(a,lp);
            asm_def_label(a,dn);
            asm_emit3(a,0x48,0x29,0xF1);   /* sub rcx,rsi */
            asm_emit3(a,0x48,0xFF,0xC1);   /* inc rcx (include NUL) */
            asm_emit2(a,0xF3,0xA4);        /* rep movsb */
        } else {
            /* Save callee-preserved regs used by shim */
            codegen_expr(cg,args[0]); asm_emit2(a,0x89,0xC7); /* mov edi,eax */
            codegen_expr(cg,args[1]); asm_emit2(a,0x89,0xC6); /* mov esi,eax */
            asm_emit2(a,0x89,0xF1);                            /* mov ecx,esi */
            int lp=asm_new_label(a,"sc_lp"),dn=asm_new_label(a,"sc_dn");
            asm_def_label(a,lp);
            asm_emit3(a,0x80,0x39,0x00);   /* cmp byte[ecx],0 */
            asm_jcc_label(a,CC_E,dn);
            asm_emit2(a,0xFF,0xC1);        /* inc ecx */
            asm_jmp_label(a,lp);
            asm_def_label(a,dn);
            asm_emit2(a,0x29,0xF1);        /* sub ecx,esi */
            asm_emit2(a,0xFF,0xC1);        /* inc ecx */
            asm_emit2(a,0xF3,0xA4);        /* rep movsb */
        }
        codegen_expr(cg,args[0]); return; /* return dst */
    }

    /* ---- strcmp(a,b) -> int ---- */
    if (strcmp(name,"strcmp")==0 && argc>=2) {
        if (cg->is_64bit) {
            codegen_expr(cg,args[0]); asm_mov_reg_reg(a,REG_RSI,REG_RAX);
            codegen_expr(cg,args[1]); asm_mov_reg_reg(a,REG_RDI,REG_RAX);
            int lp=asm_new_label(a,"scm_lp"),dn=asm_new_label(a,"scm_dn");
            asm_def_label(a,lp);
            asm_emit3(a,0x0F,0xB6,0x06);  /* movzx eax,byte[rsi] */
            asm_emit3(a,0x0F,0xB6,0x1F);  /* movzx ebx,byte[rdi] */
            asm_emit2(a,0x38,0xD8);        /* cmp al,bl */
            asm_jcc_label(a,CC_NE,dn);
            asm_emit2(a,0x84,0xC0);        /* test al,al */
            asm_jcc_label(a,CC_E,dn);
            asm_emit3(a,0x48,0xFF,0xC6);
            asm_emit3(a,0x48,0xFF,0xC7);
            asm_jmp_label(a,lp);
            asm_def_label(a,dn);
            asm_emit2(a,0x29,0xD8);        /* sub eax,ebx */
            asm_emit3(a,0x48,0x63,0xC0); /* movsxd rax,eax */
        } else {
            codegen_expr(cg,args[0]); asm_emit2(a,0x89,0xC6); /* mov esi,eax */
            codegen_expr(cg,args[1]); asm_emit2(a,0x89,0xC7); /* mov edi,eax */
            int lp=asm_new_label(a,"scm_lp"),dn=asm_new_label(a,"scm_dn");
            asm_def_label(a,lp);
            asm_emit3(a,0x0F,0xB6,0x06);
            asm_emit3(a,0x0F,0xB6,0x1F);
            asm_emit2(a,0x38,0xD8); asm_jcc_label(a,CC_NE,dn);
            asm_emit2(a,0x84,0xC0); asm_jcc_label(a,CC_E,dn);
            asm_emit2(a,0xFF,0xC6); asm_emit2(a,0xFF,0xC7);
            asm_jmp_label(a,lp);
            asm_def_label(a,dn);
            asm_emit2(a,0x29,0xD8);
        }
        return;
    }

    /* ---- strncmp(a,b,n) -> int ---- */
    if (strcmp(name,"strncmp")==0 && argc>=3) {
        if (cg->is_64bit) {
            codegen_expr(cg,args[2]); asm_mov_reg_reg(a,REG_RCX,REG_RAX);
            codegen_expr(cg,args[0]); asm_mov_reg_reg(a,REG_RSI,REG_RAX);
            codegen_expr(cg,args[1]); asm_mov_reg_reg(a,REG_RDI,REG_RAX);
            int lp=asm_new_label(a,"snc_lp"),dn=asm_new_label(a,"snc_dn");
            asm_def_label(a,lp);
            asm_emit3(a,0x48,0x85,0xC9); asm_jcc_label(a,CC_E,dn); /* test rcx,rcx */
            asm_emit3(a,0x0F,0xB6,0x06); asm_emit3(a,0x0F,0xB6,0x1F);
            asm_emit2(a,0x38,0xD8); asm_jcc_label(a,CC_NE,dn);
            asm_emit2(a,0x84,0xC0); asm_jcc_label(a,CC_E,dn);
            asm_emit3(a,0x48,0xFF,0xC6); asm_emit3(a,0x48,0xFF,0xC7); asm_emit3(a,0x48,0xFF,0xC9);
            asm_jmp_label(a,lp);
            asm_def_label(a,dn);
            asm_emit2(a,0x29,0xD8); asm_emit3(a,0x48,0x63,0xC0); /* movsxd rax,eax */
        } else {
            codegen_expr(cg,args[2]); asm_emit2(a,0x89,0xC1);
            codegen_expr(cg,args[0]); asm_emit2(a,0x89,0xC6);
            codegen_expr(cg,args[1]); asm_emit2(a,0x89,0xC7);
            int lp=asm_new_label(a,"snc_lp"),dn=asm_new_label(a,"snc_dn");
            asm_def_label(a,lp);
            asm_emit2(a,0x85,0xC9); asm_jcc_label(a,CC_E,dn);
            asm_emit3(a,0x0F,0xB6,0x06); asm_emit3(a,0x0F,0xB6,0x1F);
            asm_emit2(a,0x38,0xD8); asm_jcc_label(a,CC_NE,dn);
            asm_emit2(a,0x84,0xC0); asm_jcc_label(a,CC_E,dn);
            asm_emit2(a,0xFF,0xC6); asm_emit2(a,0xFF,0xC7); asm_emit2(a,0xFF,0xC9);
            asm_jmp_label(a,lp);
            asm_def_label(a,dn);
            asm_emit2(a,0x29,0xD8);
        }
        return;
    }

    /* ---- strcat(dst,src) -> dst ---- */
    if (strcmp(name,"strcat")==0 && argc>=2) {
        if (cg->is_64bit) {
            codegen_expr(cg,args[0]); asm_mov_reg_reg(a,REG_RDI,REG_RAX);
            int sk=asm_new_label(a,"sca_sk"); asm_def_label(a,sk);
            asm_emit3(a,0x80,0x3F,0x00); /* cmp byte[rdi],0 */
            int se=asm_new_label(a,"sca_se"); asm_jcc_label(a,CC_E,se);
            asm_emit3(a,0x48,0xFF,0xC7); asm_jmp_label(a,sk);
            asm_def_label(a,se);
            codegen_expr(cg,args[1]); asm_mov_reg_reg(a,REG_RSI,REG_RAX);
            int cp=asm_new_label(a,"sca_cp"); asm_def_label(a,cp);
            asm_emit3(a,0x0F,0xB6,0x06); /* movzx eax,byte[rsi] */
            asm_emit2(a,0x88,0x07);       /* mov byte[rdi],al */
            asm_emit2(a,0x84,0xC0);
            int ce=asm_new_label(a,"sca_ce"); asm_jcc_label(a,CC_E,ce);
            asm_emit3(a,0x48,0xFF,0xC6); asm_emit3(a,0x48,0xFF,0xC7);
            asm_jmp_label(a,cp); asm_def_label(a,ce);
        } else {
            codegen_expr(cg,args[0]); asm_emit2(a,0x89,0xC7);
            int sk=asm_new_label(a,"sca_sk"); asm_def_label(a,sk);
            asm_emit3(a,0x80,0x3F,0x00);
            int se=asm_new_label(a,"sca_se"); asm_jcc_label(a,CC_E,se);
            asm_emit2(a,0xFF,0xC7); asm_jmp_label(a,sk);
            asm_def_label(a,se);
            codegen_expr(cg,args[1]); asm_emit2(a,0x89,0xC6);
            int cp=asm_new_label(a,"sca_cp"); asm_def_label(a,cp);
            asm_emit3(a,0x0F,0xB6,0x06); asm_emit2(a,0x88,0x07); asm_emit2(a,0x84,0xC0);
            int ce=asm_new_label(a,"sca_ce"); asm_jcc_label(a,CC_E,ce);
            asm_emit2(a,0xFF,0xC6); asm_emit2(a,0xFF,0xC7); asm_jmp_label(a,cp);
            asm_def_label(a,ce);
        }
        codegen_expr(cg,args[0]); return;
    }

    /* ---- strncat(dst,src,n) -> dst ---- */
    if (strcmp(name,"strncat")==0 && argc>=3) {
        if (cg->is_64bit) {
            codegen_expr(cg,args[0]); asm_mov_reg_reg(a,REG_RDI,REG_RAX);
            int sk=asm_new_label(a,"sncat_sk"); asm_def_label(a,sk);
            asm_emit3(a,0x80,0x3F,0x00);
            int se=asm_new_label(a,"sncat_se"); asm_jcc_label(a,CC_E,se);
            asm_emit3(a,0x48,0xFF,0xC7); asm_jmp_label(a,sk); asm_def_label(a,se);
            codegen_expr(cg,args[1]); asm_mov_reg_reg(a,REG_RSI,REG_RAX);
            codegen_expr(cg,args[2]); asm_mov_reg_reg(a,REG_RCX,REG_RAX);
            int cp=asm_new_label(a,"sncat_cp"); asm_def_label(a,cp);
            asm_emit3(a,0x48,0x85,0xC9);
            int ce=asm_new_label(a,"sncat_ce"); asm_jcc_label(a,CC_E,ce);
            asm_emit3(a,0x0F,0xB6,0x06); asm_emit2(a,0x88,0x07); asm_emit2(a,0x84,0xC0);
            asm_jcc_label(a,CC_E,ce);
            asm_emit3(a,0x48,0xFF,0xC6); asm_emit3(a,0x48,0xFF,0xC7); asm_emit3(a,0x48,0xFF,0xC9);
            asm_jmp_label(a,cp); asm_def_label(a,ce);
            asm_emit2(a,0xC6,0x07); asm_emit1(a,0x00); /* mov byte[rdi],0 */
        } else {
            codegen_expr(cg,args[0]); asm_emit2(a,0x89,0xC7);
            int sk=asm_new_label(a,"sncat_sk"); asm_def_label(a,sk);
            asm_emit3(a,0x80,0x3F,0x00);
            int se=asm_new_label(a,"sncat_se"); asm_jcc_label(a,CC_E,se);
            asm_emit2(a,0xFF,0xC7); asm_jmp_label(a,sk); asm_def_label(a,se);
            codegen_expr(cg,args[1]); asm_emit2(a,0x89,0xC6);
            codegen_expr(cg,args[2]); asm_emit2(a,0x89,0xC1);
            int cp=asm_new_label(a,"sncat_cp"); asm_def_label(a,cp);
            asm_emit2(a,0x85,0xC9);
            int ce=asm_new_label(a,"sncat_ce"); asm_jcc_label(a,CC_E,ce);
            asm_emit3(a,0x0F,0xB6,0x06); asm_emit2(a,0x88,0x07); asm_emit2(a,0x84,0xC0);
            asm_jcc_label(a,CC_E,ce);
            asm_emit2(a,0xFF,0xC6); asm_emit2(a,0xFF,0xC7); asm_emit2(a,0xFF,0xC9);
            asm_jmp_label(a,cp); asm_def_label(a,ce);
            asm_emit2(a,0xC6,0x07); asm_emit1(a,0x00);
        }
        codegen_expr(cg,args[0]); return;
    }

    /* ---- strchr(s,c) -> char* or NULL ---- */
    if (strcmp(name,"strchr")==0 && argc>=2) {
        if (cg->is_64bit) {
            codegen_expr(cg,args[1]); asm_mov_reg_reg(a,REG_RBX,REG_RAX);
            codegen_expr(cg,args[0]); asm_mov_reg_reg(a,REG_RSI,REG_RAX);
            int lp=asm_new_label(a,"sch_lp"),fn=asm_new_label(a,"sch_fn"),dn=asm_new_label(a,"sch_dn");
            asm_def_label(a,lp);
            asm_emit3(a,0x0F,0xB6,0x06);  /* movzx eax,byte[rsi] */
            asm_emit2(a,0x39,0xD8);        /* cmp eax,ebx */
            asm_jcc_label(a,CC_E,fn);
            asm_emit2(a,0x84,0xC0);        /* test al,al */
            asm_jcc_label(a,CC_E,dn);
            asm_emit3(a,0x48,0xFF,0xC6); asm_jmp_label(a,lp);
            asm_def_label(a,fn); asm_mov_reg_reg(a,REG_RAX,REG_RSI);
            int rt=asm_new_label(a,"sch_rt"); asm_jmp_label(a,rt);
            asm_def_label(a,dn); asm_mov_reg_imm(a,REG_RAX,0);
            asm_def_label(a,rt);
        } else {
            codegen_expr(cg,args[1]); asm_emit2(a,0x89,0xC3); /* mov ebx,eax */
            codegen_expr(cg,args[0]); asm_emit2(a,0x89,0xC6); /* mov esi,eax */
            int lp=asm_new_label(a,"sch_lp"),fn=asm_new_label(a,"sch_fn"),dn=asm_new_label(a,"sch_dn");
            asm_def_label(a,lp);
            asm_emit3(a,0x0F,0xB6,0x06); asm_emit2(a,0x39,0xD8); asm_jcc_label(a,CC_E,fn);
            asm_emit2(a,0x84,0xC0); asm_jcc_label(a,CC_E,dn);
            asm_emit2(a,0xFF,0xC6); asm_jmp_label(a,lp);
            asm_def_label(a,fn); asm_emit2(a,0x89,0xF0); /* mov eax,esi */
            int rt=asm_new_label(a,"sch_rt"); asm_jmp_label(a,rt);
            asm_def_label(a,dn); asm_mov_reg_imm(a,REG_EAX,0);
            asm_def_label(a,rt);
        }
        return;
    }

    /* ---- strstr(hay,needle) -> char* or NULL ---- */
    if (strcmp(name,"strstr")==0 && argc>=2) {
        if (cg->is_64bit) {
            codegen_expr(cg,args[0]); asm_mov_reg_reg(a,REG_RSI,REG_RAX);
            codegen_expr(cg,args[1]); asm_mov_reg_reg(a,REG_RDI,REG_RAX);
            /* if needle is empty return haystack */
            asm_emit3(a,0x80,0x3F,0x00);
            int empty=asm_new_label(a,"ss_empty");
            asm_jcc_label(a,CC_E,empty);
            int outer=asm_new_label(a,"ss_outer"),nf=asm_new_label(a,"ss_nf");
            asm_def_label(a,outer);
            asm_emit3(a,0x80,0x3E,0x00); asm_jcc_label(a,CC_E,nf);
            /* inner compare */
            asm_push_reg(a,REG_RSI); asm_push_reg(a,REG_RDI);
            asm_mov_reg_reg(a,REG_RCX,REG_RSI); asm_mov_reg_reg(a,REG_RDX,REG_RDI);
            int inner=asm_new_label(a,"ss_inner"),match=asm_new_label(a,"ss_match"),nm=asm_new_label(a,"ss_nm");
            asm_def_label(a,inner);
            asm_emit3(a,0x80,0x3A,0x00); asm_jcc_label(a,CC_E,match);
            asm_emit3(a,0x0F,0xB6,0x01); asm_emit3(a,0x0F,0xB6,0x1A);
            asm_emit2(a,0x38,0xD8); asm_jcc_label(a,CC_NE,nm);
            asm_emit3(a,0x48,0xFF,0xC1); asm_emit3(a,0x48,0xFF,0xC2);
            asm_jmp_label(a,inner);
            asm_def_label(a,match);
            asm_pop_reg(a,REG_RDI); asm_pop_reg(a,REG_RSI);
            asm_mov_reg_reg(a,REG_RAX,REG_RSI);
            int rt=asm_new_label(a,"ss_rt"); asm_jmp_label(a,rt);
            asm_def_label(a,nm);
            asm_pop_reg(a,REG_RDI); asm_pop_reg(a,REG_RSI);
            asm_emit3(a,0x48,0xFF,0xC6); asm_jmp_label(a,outer);
            asm_def_label(a,nf); asm_mov_reg_imm(a,REG_RAX,0);
            asm_jmp_label(a,rt);
            asm_def_label(a,empty); asm_mov_reg_reg(a,REG_RAX,REG_RSI);
            asm_def_label(a,rt);
        } else {
            codegen_expr(cg,args[0]); asm_emit2(a,0x89,0xC6);
            codegen_expr(cg,args[1]); asm_emit2(a,0x89,0xC7);
            asm_emit3(a,0x80,0x3F,0x00);
            int empty=asm_new_label(a,"ss_empty"); asm_jcc_label(a,CC_E,empty);
            int outer=asm_new_label(a,"ss_outer"),nf=asm_new_label(a,"ss_nf");
            asm_def_label(a,outer);
            asm_emit3(a,0x80,0x3E,0x00); asm_jcc_label(a,CC_E,nf);
            asm_push_reg(a,REG_ESI); asm_push_reg(a,REG_EDI);
            asm_emit2(a,0x89,0xF1); asm_emit2(a,0x89,0xFA);
            int inner=asm_new_label(a,"ss_inner"),match=asm_new_label(a,"ss_match"),nm=asm_new_label(a,"ss_nm");
            asm_def_label(a,inner);
            asm_emit3(a,0x80,0x3A,0x00); asm_jcc_label(a,CC_E,match);
            asm_emit3(a,0x0F,0xB6,0x01); asm_emit3(a,0x0F,0xB6,0x1A);
            asm_emit2(a,0x38,0xD8); asm_jcc_label(a,CC_NE,nm);
            asm_emit2(a,0xFF,0xC1); asm_emit2(a,0xFF,0xC2); asm_jmp_label(a,inner);
            asm_def_label(a,match);
            asm_pop_reg(a,REG_EDI); asm_pop_reg(a,REG_ESI);
            asm_emit2(a,0x89,0xF0);
            int rt=asm_new_label(a,"ss_rt"); asm_jmp_label(a,rt);
            asm_def_label(a,nm);
            asm_pop_reg(a,REG_EDI); asm_pop_reg(a,REG_ESI);
            asm_emit2(a,0xFF,0xC6); asm_jmp_label(a,outer);
            asm_def_label(a,nf); asm_mov_reg_imm(a,REG_EAX,0);
            asm_jmp_label(a,rt);
            asm_def_label(a,empty); asm_emit2(a,0x89,0xF0);
            asm_def_label(a,rt);
        }
        return;
    }

    /* ---- strncpy(dst,src,n) -> dst ---- */
    if (strcmp(name,"strncpy")==0 && argc>=3) {
        if (cg->is_64bit) {
            codegen_expr(cg,args[2]); asm_mov_reg_reg(a,REG_RCX,REG_RAX);
            /* dst: array member fields must pass address, not loaded value */
            if (args[0]->kind == AST_MEMBER)
                codegen_lvalue(cg,args[0]);
            else
                codegen_expr(cg,args[0]);
            asm_mov_reg_reg(a,REG_RDI,REG_RAX);
            codegen_expr(cg,args[1]); asm_mov_reg_reg(a,REG_RSI,REG_RAX);
            int lp=asm_new_label(a,"sncp_lp"),dn=asm_new_label(a,"sncp_dn");
            asm_def_label(a,lp);
            asm_emit3(a,0x48,0x85,0xC9); asm_jcc_label(a,CC_E,dn);
            asm_emit3(a,0x0F,0xB6,0x06); asm_emit2(a,0x88,0x07);
            asm_emit2(a,0x84,0xC0); asm_jcc_label(a,CC_E,dn);
            asm_emit3(a,0x48,0xFF,0xC6); asm_emit3(a,0x48,0xFF,0xC7); asm_emit3(a,0x48,0xFF,0xC9);
            asm_jmp_label(a,lp); asm_def_label(a,dn);
        } else {
            codegen_expr(cg,args[2]); asm_emit2(a,0x89,0xC1);
            if (args[0]->kind == AST_MEMBER) codegen_lvalue(cg,args[0]); else codegen_expr(cg,args[0]);
            asm_emit2(a,0x89,0xC7);
            codegen_expr(cg,args[1]); asm_emit2(a,0x89,0xC6);
            int lp=asm_new_label(a,"sncp_lp"),dn=asm_new_label(a,"sncp_dn");
            asm_def_label(a,lp);
            asm_emit2(a,0x85,0xC9); asm_jcc_label(a,CC_E,dn);
            asm_emit3(a,0x0F,0xB6,0x06); asm_emit2(a,0x88,0x07);
            asm_emit2(a,0x84,0xC0); asm_jcc_label(a,CC_E,dn);
            asm_emit2(a,0xFF,0xC6); asm_emit2(a,0xFF,0xC7); asm_emit2(a,0xFF,0xC9);
            asm_jmp_label(a,lp); asm_def_label(a,dn);
        }
        if (args[0]->kind == AST_MEMBER) codegen_lvalue(cg,args[0]); else codegen_expr(cg,args[0]);
        return;
    }

    /* ---- abs(x) / labs(x) -> absolute value ---- */
    if ((strcmp(name,"abs")==0 || strcmp(name,"labs")==0) && argc>=1) {
        codegen_expr(cg,args[0]);  /* result in RAX/EAX */
        if (cg->is_64bit) {
            /* 64-bit: test rax,rax; jns skip; neg rax; skip: */
            int skip=asm_new_label(a,"abs_skip");
            asm_test_reg_reg(a,REG_RAX,REG_RAX);
            asm_jcc_label(a,CC_GE,skip);
            asm_emit3(a,0x48,0xF7,0xD8); /* neg rax */
            asm_def_label(a,skip);
        } else {
            /* 32-bit: test eax,eax; jns skip; neg eax; skip: */
            int skip=asm_new_label(a,"abs_skip");
            asm_test_reg_reg(a,REG_EAX,REG_EAX);
            asm_jcc_label(a,CC_GE,skip);
            asm_emit2(a,0xF7,0xD8); /* neg eax */
            asm_def_label(a,skip);
        }
        return;
    }

        /* ---- atoi/atol(s) -> int ---- */
    if ((strcmp(name,"atoi")==0||strcmp(name,"atol")==0) && argc>=1) {
        if (cg->is_64bit) {
            codegen_expr(cg,args[0]); asm_mov_reg_reg(a,REG_RSI,REG_RAX);
            /* skip whitespace: loop while al < 0x21 and al != 0 */
            int ws=asm_new_label(a,"atoi_ws"),wsd=asm_new_label(a,"atoi_wsd");
            asm_def_label(a,ws);
            asm_emit3(a,0x0F,0xB6,0x06);           /* movzx eax,byte[rsi] */
            asm_emit2(a,0x84,0xC0); asm_jcc_label(a,CC_E,wsd);     /* nul -> done */
            asm_emit2(a,0x3C,0x21); asm_jcc_label(a,CC_GE,wsd);    /* >=0x21 -> done */
            asm_emit3(a,0x48,0xFF,0xC6); asm_jmp_label(a,ws);
            asm_def_label(a,wsd);
            /* sign */
            asm_mov_reg_imm(a,REG_RBX,1);
            asm_emit2(a,0x3C,0x2D);                 /* cmp al,'-' */
            int nosg=asm_new_label(a,"atoi_nosg"); asm_jcc_label(a,CC_NE,nosg);
            asm_mov_reg_imm(a,REG_RBX,-1);
            asm_emit3(a,0x48,0xFF,0xC6);
            asm_emit3(a,0x0F,0xB6,0x06);
            asm_def_label(a,nosg);
            asm_mov_reg_imm(a,REG_RDI,0);
            int dlp=asm_new_label(a,"atoi_dlp"),ddn=asm_new_label(a,"atoi_ddn");
            asm_def_label(a,dlp);
            asm_emit2(a,0x3C,0x30); asm_jcc_label(a,CC_L,ddn);
            asm_emit2(a,0x3C,0x3A); asm_jcc_label(a,CC_GE,ddn);
            asm_emit3(a,0x48,0x6B,0xFF); asm_emit1(a,10); /* imul rdi,rdi,10 */
            asm_emit3(a,0x0F,0xB6,0xC0);                   /* movzx eax,al */
            asm_emit3(a,0x83,0xE8,0x30);                   /* sub eax,'0' */
            asm_emit3(a,0x48,0x01,0xC7);                   /* add rdi,rax */
            asm_emit3(a,0x48,0xFF,0xC6);
            asm_emit3(a,0x0F,0xB6,0x06);
            asm_jmp_label(a,dlp);
            asm_def_label(a,ddn);
            asm_mov_reg_reg(a,REG_RAX,REG_RDI);
            asm_emit3(a,0x48,0xF7,0xEB);  /* imul rbx -> rdx:rax = rax*rbx */
        } else {
            codegen_expr(cg,args[0]); asm_emit2(a,0x89,0xC6);
            int ws=asm_new_label(a,"atoi_ws"),wsd=asm_new_label(a,"atoi_wsd");
            asm_def_label(a,ws);
            asm_emit3(a,0x0F,0xB6,0x06);
            asm_emit2(a,0x84,0xC0); asm_jcc_label(a,CC_E,wsd);
            asm_emit2(a,0x3C,0x21); asm_jcc_label(a,CC_GE,wsd);
            asm_emit2(a,0xFF,0xC6); asm_jmp_label(a,ws);
            asm_def_label(a,wsd);
            asm_mov_reg_imm(a,REG_EBX,1);
            asm_emit2(a,0x3C,0x2D);
            int nosg=asm_new_label(a,"atoi_nosg"); asm_jcc_label(a,CC_NE,nosg);
            asm_mov_reg_imm(a,REG_EBX,-1);
            asm_emit2(a,0xFF,0xC6); asm_emit3(a,0x0F,0xB6,0x06);
            asm_def_label(a,nosg);
            asm_mov_reg_imm(a,REG_EDI,0);
            int dlp=asm_new_label(a,"atoi_dlp"),ddn=asm_new_label(a,"atoi_ddn");
            asm_def_label(a,dlp);
            asm_emit2(a,0x3C,0x30); asm_jcc_label(a,CC_L,ddn);
            asm_emit2(a,0x3C,0x3A); asm_jcc_label(a,CC_GE,ddn);
            asm_emit3(a,0x6B,0xFF,0x0A);
            asm_emit3(a,0x0F,0xB6,0xC0); asm_emit3(a,0x83,0xE8,0x30);
            asm_emit2(a,0x01,0xC7);
            asm_emit2(a,0xFF,0xC6); asm_emit3(a,0x0F,0xB6,0x06);
            asm_jmp_label(a,dlp);
            asm_def_label(a,ddn);
            asm_emit2(a,0x89,0xF8);
            asm_emit2(a,0xF7,0xEB);
        }
        return;
    }

    /* ---- atof(s) -> double  (result in XMM0 64-bit / ST0 32-bit) ---- */
    if (strcmp(name,"atof")==0 && argc>=1) {
        codegen_expr(cg,args[0]); /* RAX/EAX = string ptr */
        if (cg->is_64bit) {
            /* 64-bit: forward to msvcrt atof; arg in RCX, result in XMM0 */
            symtable_add_import(cg->sym,"msvcrt.dll:atof");
            asm_mov_reg_reg(a,REG_RCX,REG_RAX);
            asm_sub_rsp(a,40);
            asm_call_import(a,"atof");
            asm_add_rsp(a,40);
        } else {
            /* 32-bit inline x87 atof:
             * On entry EAX = string ptr.
             * Registers used: ESI=ptr, ECX=cur_char, EDX=sign, EBX=frac_divisor
             * x87 stack during int  loop: ST0=int_accum
             * x87 stack during frac loop: ST0=frac_accum, ST1=int_accum
             * After frac: push EBX; fidiv [esp]; faddp -> ST0=int+frac
             */
            /* Save registers */
            asm_push_reg(a,REG_EBX);
            asm_push_reg(a,REG_ESI);
            asm_push_reg(a,REG_EDI);
            asm_mov_reg_reg(a,REG_ESI,REG_EAX); /* ESI = ptr */

            /* Skip leading whitespace */
            int af_ws=asm_new_label(a,"atof_ws"),af_wsd=asm_new_label(a,"atof_wsd");
            asm_def_label(a,af_ws);
            asm_emit3(a,0x0F,0xB6,0x0E);          /* movzx ecx,byte[esi] */
            asm_emit3(a,0x83,0xF9,0x20);           /* cmp ecx,0x20 */
            asm_jcc_label(a,CC_NE,af_wsd);
            asm_emit2(a,0xFF,0xC6);                /* inc esi */
            asm_jmp_label(a,af_ws);
            asm_def_label(a,af_wsd);

            /* Sign: EDX=0 positive, EDX=1 negative */
            int af_sgn=asm_new_label(a,"atof_sgn"),af_sgo=asm_new_label(a,"atof_sgo");
            asm_emit1(a,0x31); asm_emit1(a,0xD2); /* xor edx,edx */
            asm_emit3(a,0x80,0x3E,0x2D);           /* cmp byte[esi],'-' */
            asm_jcc_label(a,CC_NE,af_sgn);
            asm_emit1(a,0x42);                      /* inc edx */
            asm_emit2(a,0xFF,0xC6);                /* inc esi */
            asm_jmp_label(a,af_sgo);
            asm_def_label(a,af_sgn);
            asm_emit3(a,0x80,0x3E,0x2B);           /* cmp byte[esi],'+' */
            asm_jcc_label(a,CC_NE,af_sgo);
            asm_emit2(a,0xFF,0xC6);                /* inc esi */
            asm_def_label(a,af_sgo);

            /* Integer part: ST0 = 0.0, loop ST0=ST0*10+digit */
            int af_ilp=asm_new_label(a,"atof_ilp"),af_idn=asm_new_label(a,"atof_idn");
            asm_fldz(a);                            /* ST0=0.0 */
            asm_def_label(a,af_ilp);
            asm_emit3(a,0x0F,0xB6,0x0E);           /* movzx ecx,byte[esi] */
            asm_emit3(a,0x83,0xE9,0x30);           /* sub ecx,'0' */
            asm_emit3(a,0x83,0xF9,0x00);           /* cmp ecx,0 */
            asm_jcc_label(a,CC_L,af_idn);
            asm_emit3(a,0x83,0xF9,0x09);           /* cmp ecx,9 */
            asm_jcc_label(a,CC_G,af_idn);
            /* ST0 = ST0*10 */
            asm_sub_rsp(a,4);
            asm_emit3(a,0xC7,0x04,0x24); asm_emit_u32(a,10); /* mov dword[esp],10 */
            asm_emit3(a,0xDA,0x0C,0x24);           /* fimul dword[esp] */
            /* ST0 += digit */
            asm_emit3(a,0x89,0x0C,0x24);           /* mov [esp],ecx */
            asm_emit3(a,0xDA,0x04,0x24);           /* fiadd dword[esp] */
            asm_add_rsp(a,4);
            asm_emit2(a,0xFF,0xC6);                /* inc esi */
            asm_jmp_label(a,af_ilp);
            asm_def_label(a,af_idn);

            /* Fractional part */
            int af_fno=asm_new_label(a,"atof_fno"),af_flp=asm_new_label(a,"atof_flp");
            int af_fdn=asm_new_label(a,"atof_fdn");
            asm_emit3(a,0x80,0x3E,0x2E);           /* cmp byte[esi],'.' */
            asm_jcc_label(a,CC_NE,af_fno);
            asm_emit2(a,0xFF,0xC6);                /* inc esi */
            /* EBX = divisor (starts at 1) */
            asm_emit1(a,0xBB); asm_emit_u32(a,1); /* mov ebx,1 */
            asm_fldz(a);                           /* ST0=0.0 (frac), ST1=int */
            asm_def_label(a,af_flp);
            asm_emit3(a,0x0F,0xB6,0x0E);          /* movzx ecx,byte[esi] */
            asm_emit3(a,0x83,0xE9,0x30);          /* sub ecx,'0' */
            asm_emit3(a,0x83,0xF9,0x00);          /* cmp ecx,0 */
            asm_jcc_label(a,CC_L,af_fdn);
            asm_emit3(a,0x83,0xF9,0x09);          /* cmp ecx,9 */
            asm_jcc_label(a,CC_G,af_fdn);
            /* ST0 = ST0*10 + digit */
            asm_sub_rsp(a,4);
            asm_emit3(a,0xC7,0x04,0x24); asm_emit_u32(a,10);
            asm_emit3(a,0xDA,0x0C,0x24);          /* fimul dword[esp] */
            asm_emit3(a,0x89,0x0C,0x24);          /* mov [esp],ecx */
            asm_emit3(a,0xDA,0x04,0x24);          /* fiadd dword[esp] */
            asm_add_rsp(a,4);
            /* EBX *= 10 */
            asm_emit3(a,0x6B,0xDB,0x0A);          /* imul ebx,ebx,10 */
            asm_emit2(a,0xFF,0xC6);               /* inc esi */
            asm_jmp_label(a,af_flp);
            asm_def_label(a,af_fdn);
            /* ST0=frac_int, ST1=int_val; EBX=divisor */
            /* frac = frac_int / EBX */
            asm_sub_rsp(a,4);
            asm_emit3(a,0x89,0x1C,0x24);          /* mov [esp],ebx */
            asm_emit3(a,0xDA,0x34,0x24);          /* fidiv dword[esp] -> ST0 /= EBX */
            asm_add_rsp(a,4);
            /* result = int + frac */
            asm_emit2(a,0xDE,0xC1);               /* faddp st1,st0 -> ST0=int+frac */
            asm_def_label(a,af_fno);

            /* Apply sign */
            int af_pos=asm_new_label(a,"atof_pos");
            asm_emit2(a,0x85,0xD2);               /* test edx,edx */
            asm_jcc_label(a,CC_E,af_pos);
            asm_emit2(a,0xD9,0xE0);               /* fchs */
            asm_def_label(a,af_pos);

            /* Restore registers */
            asm_pop_reg(a,REG_EDI);
            asm_pop_reg(a,REG_ESI);
            asm_pop_reg(a,REG_EBX);
            /* Result in ST0 */
        }
        return;
    }

    /* ---- snprintf → _snprintf (msvcrt.dll doesn't export snprintf w/o underscore) ---- */
    if (strcmp(name,"snprintf")==0) {
        symtable_add_import(cg->sym, "msvcrt.dll:_snprintf");
        Assembler *a2 = cg->asm_;
        if (cg->is_64bit) {
            /* Eval args 0..3 into RCX/RDX/R8/R9 FIRST (no shadow yet), saving
             * already-placed args to R10-R13 whenever a later arg's own
             * evaluation contains a nested call (which clobbers the volatile
             * RCX/RDX/R8/R9 registers) — same fix as the printf shim below;
             * this one was missing it entirely, corrupting buf/size/fmt
             * whenever a vararg was itself a function call (e.g. SDL_Log's
             * "%s%s", GetLogPriorityPrefix(priority), message). */
#define ARG_REG(i)  ((i)==0?REG_RCX:(i)==1?REG_RDX:(i)==2?REG_R8:REG_R9)
#define SAVE_REG(k) ((k)==0?REG_R10:(k)==1?REG_R11:(k)==2?REG_R12:REG_R13)
            for (int i=0;i<argc&&i<4;i++) {
                int this_has_call = expr_has_call(args[i]);
                if (this_has_call)
                    for (int k=0; k<i; k++) asm_mov_reg_reg(a2, SAVE_REG(k), ARG_REG(k));
                codegen_expr(cg, args[i]);
                asm_mov_reg_reg(a2,ARG_REG(i),REG_RAX);
                if (this_has_call)
                    for (int k=0; k<i; k++) asm_mov_reg_reg(a2, ARG_REG(k), SAVE_REG(k));
            }
#undef ARG_REG
#undef SAVE_REG
            int extra = (argc > 4) ? (argc-4)*8 : 0;
            int frame = 32 + extra; if ((frame&8)==0) frame+=8;
            asm_sub_rsp(a2, frame);
            /* Stack args (i>=4) are evaluated after RCX/RDX/R8/R9 already hold
             * args 0..3 — protect those registers the same way if a stack
             * arg's own evaluation contains a nested call. */
            for (int i=4; i<argc; i++) {
                int this_has_call = expr_has_call(args[i]);
                if (this_has_call) {
                    asm_mov_reg_reg(a2,REG_R10,REG_RCX); asm_mov_reg_reg(a2,REG_R11,REG_RDX);
                    asm_mov_reg_reg(a2,REG_R12,REG_R8);  asm_mov_reg_reg(a2,REG_R13,REG_R9);
                }
                codegen_expr(cg, args[i]);
                asm_mov_mem_reg(a2,REG_RSP,32+(i-4)*8,REG_RAX);
                if (this_has_call) {
                    asm_mov_reg_reg(a2,REG_RCX,REG_R10); asm_mov_reg_reg(a2,REG_RDX,REG_R11);
                    asm_mov_reg_reg(a2,REG_R8,REG_R12);  asm_mov_reg_reg(a2,REG_R9,REG_R13);
                }
            }
            asm_call_import(a2,"_snprintf");
            asm_add_rsp(a2, frame);
        } else {
            for (int i=argc-1; i>=0; i--) { codegen_expr(cg,args[i]); asm_push_reg(a2,REG_EAX); }
            asm_call_import32(a2,"_snprintf"); if (argc>0) asm_add_rsp(a2,argc*4);
        }
        return;
    }

    /* ---- fprintf: direct msvcrt.dll call ---- */
    if (strcmp(name,"fprintf")==0) {
        /* Pass directly to msvcrt fprintf which handles FILE* + fmt + varargs */
        symtable_add_import(cg->sym, "msvcrt.dll:fprintf");
        Assembler *a2 = cg->asm_;
        if (cg->is_64bit) {
            /* See snprintf shim above for why this save/restore is needed. */
#define ARG_REG(i)  ((i)==0?REG_RCX:(i)==1?REG_RDX:(i)==2?REG_R8:REG_R9)
#define SAVE_REG(k) ((k)==0?REG_R10:(k)==1?REG_R11:(k)==2?REG_R12:REG_R13)
            for (int i=0;i<argc&&i<4;i++) {
                int this_has_call = expr_has_call(args[i]);
                if (this_has_call)
                    for (int k=0; k<i; k++) asm_mov_reg_reg(a2, SAVE_REG(k), ARG_REG(k));
                codegen_expr(cg, args[i]);
                asm_mov_reg_reg(a2,ARG_REG(i),REG_RAX);
                if (this_has_call)
                    for (int k=0; k<i; k++) asm_mov_reg_reg(a2, ARG_REG(k), SAVE_REG(k));
            }
#undef ARG_REG
#undef SAVE_REG
            int extra = (argc > 4) ? (argc-4)*8 : 0;
            int frame = 32 + extra; if ((frame&8)==0) frame+=8;
            asm_sub_rsp(a2, frame);
            /* See snprintf shim above for why this save/restore is needed. */
            for (int i=4; i<argc; i++) {
                int this_has_call = expr_has_call(args[i]);
                if (this_has_call) {
                    asm_mov_reg_reg(a2,REG_R10,REG_RCX); asm_mov_reg_reg(a2,REG_R11,REG_RDX);
                    asm_mov_reg_reg(a2,REG_R12,REG_R8);  asm_mov_reg_reg(a2,REG_R13,REG_R9);
                }
                codegen_expr(cg, args[i]);
                asm_mov_mem_reg(a2,REG_RSP,32+(i-4)*8,REG_RAX);
                if (this_has_call) {
                    asm_mov_reg_reg(a2,REG_RCX,REG_R10); asm_mov_reg_reg(a2,REG_RDX,REG_R11);
                    asm_mov_reg_reg(a2,REG_R8,REG_R12);  asm_mov_reg_reg(a2,REG_R9,REG_R13);
                }
            }
            asm_call_import(a2,"fprintf");
            asm_add_rsp(a2, frame);
        } else {
            for (int i=argc-1; i>=0; i--) { codegen_expr(cg,args[i]); asm_push_reg(a2,REG_EAX); }
            asm_call_import32(a2,"fprintf"); if (argc>0) asm_add_rsp(a2,argc*4);
        }
        return;
    }

    /* ---- exit(code): call ExitProcess(code) ---- */
    if (strcmp(name,"exit")==0 || strcmp(name,"abort")==0) {
        long long code_val = 0;
        if (strcmp(name,"exit")==0 && argc>=1) {
            codegen_expr(cg, args[0]);
            /* result in RAX/EAX - move to arg register for ExitProcess */
        } else {
            asm_mov_reg_imm(a, REG_RAX, 0);
        }
        symtable_add_import(cg->sym, "KERNEL32.dll:ExitProcess");
        if (cg->is_64bit) {
            asm_sub_rsp(a, 40);
            asm_mov_reg_reg(a, REG_RCX, REG_RAX);
            asm_call_import(a, "ExitProcess");
            asm_add_rsp(a, 40);
        } else {
            asm_push_reg(a, REG_EAX);
            asm_call_import32(a, "ExitProcess");
            asm_add_rsp(a, 4);
        }
        return;
    }

    /* ---- msvcrt.dll file I/O passthrough shims ---- */
    /* Avoid static pointer array (squash codegen can't init them); use explicit strcmp. */
    {
        int is_file_fn = (
            strcmp(name,"fopen")==0||strcmp(name,"fclose")==0||strcmp(name,"fgets")==0||
            strcmp(name,"fputs")==0||strcmp(name,"feof")==0||strcmp(name,"fflush")==0||
            strcmp(name,"fread")==0||strcmp(name,"fwrite")==0||strcmp(name,"fseek")==0||
            strcmp(name,"ftell")==0||strcmp(name,"rewind")==0||
            strcmp(name,"fgetc")==0||strcmp(name,"fputc")==0||strcmp(name,"ungetc")==0||
            strcmp(name,"remove")==0||strcmp(name,"rename")==0||strcmp(name,"ferror")==0||
            strcmp(name,"clearerr")==0||strcmp(name,"strdup")==0||strcmp(name,"perror")==0||
            strcmp(name,"strerror")==0||strcmp(name,"getenv")==0||strcmp(name,"system")==0||
            strcmp(name,"strrchr")==0||strcmp(name,"strtok")==0||strcmp(name,"strpbrk")==0||
            strcmp(name,"vfprintf")==0||strcmp(name,"vprintf")==0||
            strcmp(name,"_stricmp")==0||strcmp(name,"_strnicmp")==0||
            strcmp(name,"sprintf")==0||strcmp(name,"vsnprintf")==0||strcmp(name,"putchar")==0||
            strcmp(name,"strtol")==0||strcmp(name,"strtoul")==0||strcmp(name,"strtod")==0||
            strcmp(name,"strtoll")==0||strcmp(name,"strtoull")==0||
            strcmp(name,"sscanf")==0||strcmp(name,"scanf")==0||
            strcmp(name,"isxdigit")==0||strcmp(name,"isupper")==0||strcmp(name,"islower")==0);
        if (is_file_fn) {
            if (cg->is_linux) {
                /* Unreachable when is_linux (this function returns earlier
                 * above for every name not in the 9-name inline-shim set,
                 * which is_file_fn's names never overlap with) -- kept
                 * as-is as a defensive fallback; see the real -linux
                 * dispatch and its _stricmp/_strnicmp handling near the
                 * top of this function. */
                emit_linux_libc_call(cg, name, args, argc);
                return;
            }
            char import_key[64];
            /* Use snprintf/vsnprintf directly (modern Windows msvcrt.dll supports them) */
            const char *import_name = name;
            snprintf(import_key, sizeof import_key, "msvcrt.dll:%s", import_name);
            symtable_add_import(cg->sym, import_key);
            if (cg->is_64bit) {
                /* 64-bit: args in RCX,RDX,R8,R9; shadow space */
                int extra = (argc > 4) ? (argc-4)*8 : 0;
                int frame = 32 + extra;
                if ((frame & 8) == 0) frame += 8;
                asm_sub_rsp(a, frame);
                for (int i=0; i<argc; i++) {
                    codegen_expr(cg, args[i]);
                    if      (i==0) asm_mov_reg_reg(a,REG_RCX,REG_RAX);
                    else if (i==1) asm_mov_reg_reg(a,REG_RDX,REG_RAX);
                    else if (i==2) asm_mov_reg_reg(a,REG_R8, REG_RAX);
                    else if (i==3) asm_mov_reg_reg(a,REG_R9, REG_RAX);
                    else           asm_mov_mem_reg(a,REG_RSP,32+(i-4)*8,REG_RAX);
                }
                asm_call_import(a, name);
                asm_add_rsp(a, frame);
            } else {
                /* 32-bit cdecl: args right-to-left */
                for (int i=argc-1; i>=0; i--) {
                    codegen_expr(cg, args[i]);
                    asm_push_reg(a, REG_EAX);
                }
                asm_call_import32(a, name);
                if (argc > 0) asm_add_rsp(a, argc*4);
            }
            return;
        }
    }

    /* Default for all other internal shims — evaluate args and return 0 */
    for (int i = 0; i < argc; i++) codegen_expr(cg, args[i]);
    asm_mov_reg_imm(a, REG_RAX, 0);
}

/* =========================================================================
 * codegen_lvalue — compute address of lvalue into RAX/EAX
 * ========================================================================= */
void codegen_lvalue(CodeGen *cg, ASTNode *n) {
    Assembler *a=cg->asm_;
    if (!n) return;
    switch (n->kind) {
    case AST_VAR: {
        Symbol *s=symtable_lookup(cg->sym,n->var.name);
        if (!s) {
            char sugbuf[160] = "";
            const char *suggestion = diag_suggest_name(cg->sym, n->var.name);
            if (suggestion) snprintf(sugbuf, sizeof sugbuf, " (did you mean '%s'?)", suggestion);
            diag_emit(DIAG_ERROR, n->line, cg->cur_func_name, n->var.name,
                      "undefined identifier '%s' used as an lvalue%s", n->var.name, sugbuf);
            asm_mov_reg_imm(a,REG_RAX,0); return;
        }
        if (s->kind==SYM_VAR||s->kind==SYM_PARAM)
            asm_lea_rbp_disp(a,REG_RAX,s->offset);
        else if (s->kind==SYM_GLOBAL) {
            /* All globals live in wdata (.data section, writable) */
            const char *lbl = (s->dll && s->dll[0]) ? s->dll : n->var.name;
            if (cg->is_64bit) {
                asm_lea_rip_wdata(a,REG_RAX,lbl);
                if (0) /* suppress old rdata path */ asm_lea_rip_data(a,REG_RAX,lbl);
            } else {
                asm_emit1(a,0xB8); asm_reloc_wdata(a,lbl);
            }
        } else {
            asm_mov_reg_imm(a,REG_RAX,0);
        }
        break;
    }
    case AST_INDEX: {
        /* &array[idx] = base_addr + idx * elem_size                         */
        if (!n->index.array || !n->index.index) { asm_mov_reg_imm(a,REG_RAX,0); return; }
        int esz = elem_size_of(cg, n->index.array);
        /* Determine whether the array base is a pointer VALUE (use codegen_expr)
         * or a stack/inline array ADDRESS (use codegen_lvalue).
         * Pointer base: pointer VAR, struct pointer field, deref, call, etc.
         * Array base:   stack VAR array, inline array field in struct.       */
        int is_pointer_base = 0;
        if (n->index.array->kind == AST_VAR) {
            Symbol *asym = symtable_lookup(cg->sym, n->index.array->var.name);
            if (asym && asym->type) {
                int is_stack_array = (asym->array_size > 0);
                if (asym->type->pointer_depth > 0 && !is_stack_array)
                    is_pointer_base = 1;
            }
        } else if (n->index.array->kind == AST_MEMBER) {
            /* ptr->field[i] or struct.field[i]:
             * pointer base if the field has pointer_depth > 0 (char*, void*, etc.),
             * array base if the field is an inline fixed-size array.
             * For nested chains like prog->program.decls, use resolve_node_type. */
            ASTNode *mnode = n->index.array;
            ASTNode *mobj  = mnode->member.obj;
            const char *mstype = NULL;
            if (mobj && mobj->kind == AST_VAR) {
                Symbol *sv = symtable_lookup(cg->sym, mobj->var.name);
                if (sv && sv->type) mstype = sv->type->base;
            } else if (mobj) {
                mstype = resolve_node_type(cg->sym, mobj);
            }
            if (mstype) {
                const char *bare = mstype;
                if      (strncmp(bare,"struct ",7)==0) bare+=7;
                else if (strncmp(bare,"union ", 6)==0) bare+=6;
                else {
                    Symbol *td = symtable_lookup(cg->sym, mstype);
                    if (td && td->kind==SYM_TYPEDEF && td->type) {
                        const char *tb = td->type->base;
                        if      (strncmp(tb,"struct ",7)==0) bare = tb+7;
                        else if (strncmp(tb,"union ", 6)==0) bare = tb+6;
                        else bare = tb;
                    }
                }
                char sk[256]; snprintf(sk,sizeof sk,"struct %s",bare);
                Symbol *ss = symtable_lookup(cg->sym, sk);
                if (ss && ss->struct_node) {
                    const char *fname = mnode->member.field;
                    for (int _fi=0; _fi<ss->struct_node->struct_decl.nfields; _fi++) {
                        ASTNode *ff = ss->struct_node->struct_decl.fields[_fi];
                        if (ff && ff->field.name &&
                            strcmp(ff->field.name, fname)==0 && ff->field.type) {
                            /* Inline array fields (array_size > 0) are address-based
                             * even if element type has pointer_depth > 0 (e.g. char *[16]).
                             * Only pure pointer fields (not arrays) are pointer-based. */
                            if (ff->field.type->pointer_depth > 0 && ff->field.array_size <= 0)
                                is_pointer_base = 1;
                            break;
                        }
                    }
                }
            }
        } else {
            /* AST_DEREF, AST_CALL, nested AST_INDEX, cast, etc.: always a pointer */
            is_pointer_base = 1;
        }

        /* Load the base: either the VALUE of the pointer (ptr[i])
         * or the ADDRESS of the array's first element (arr[i]). */
        if (is_pointer_base) {
            codegen_expr(cg, n->index.array);   /* RAX = pointer value (heap addr) */
        } else {
            codegen_lvalue(cg, n->index.array); /* RAX = &arr[0] (frame-relative)  */
        }
        asm_push_reg(a, REG_RAX);               /* save base address */

        codegen_expr(cg, n->index.index);       /* RAX = index */
        if (esz > 1) {
            if (cg->is_64bit) {
                asm_mov_reg_imm(a, REG_RBX, (long long)esz);
                asm_imul_reg_reg(a, REG_RAX, REG_RBX);
            } else {
                asm_mov_reg_imm(a, REG_EBX, (long long)esz);
                asm_imul_reg_reg(a, REG_EAX, REG_EBX);
            }
        }
        asm_pop_reg(a, REG_RBX);                /* RBX = base address */
        asm_add_reg_reg(a, REG_RAX, REG_RBX);  /* RAX = &base[idx]   */
        break;
    }
    case AST_DEREF:
        codegen_expr(cg,n->deref.operand);
        break;
    case AST_MEMBER: {
        /* Get base address of struct/union into RAX/EAX */
        if (n->member.arrow) codegen_expr(cg,n->member.obj);
        else codegen_lvalue(cg,n->member.obj);
        /* Add field byte offset (0 for first field or union) */
        { int foff = field_byte_offset(cg->sym, n->member.obj, n->member.field);
          if (foff > 0) asm_add_imm(cg->asm_, REG_RAX, foff); }
        break;
    }
    default:
        codegen_expr(cg,n);
        break;
    }
}


static void codegen_branch(CodeGen *cg, ASTNode *cond, int lbl, int jump_if_true);
/* Forward declarations for typedef-resolution helpers */
static int is_float_type(CodeGen *cg, TypeInfo *t);
static int effective_typeinfo_size(CodeGen *cg, TypeInfo *t, int is_64bit);
static int expr_has_call(ASTNode *n);

/* movq gpr64, xmm — raw 64-bit bit-copy (NOT a float->int conversion) from an
 * XMM register into the matching general-purpose register. Windows x64's
 * calling convention requires this for EVERY variadic call argument in
 * positions 0-3 (printf, vsnprintf, etc.): since the callee has no compile-
 * time-known type for a "..." slot, it always spills the shadow-space copy
 * from the integer register (RCX/RDX/R8/R9), never from XMM, regardless of
 * whether the actual argument is a float/double. Without this duplication,
 * every %f/%lf-style variadic argument silently reads as whatever garbage
 * was already sitting in that integer register — confirmed via a minimal
 * repro: even "printf(\"%f\n\", 1.5);" printed 0.000000. Emitted
 * unconditionally for any float call argument (not just genuinely variadic
 * callees) since a normal typed callee simply never reads the redundant
 * integer register for a float parameter — safe either way. */
static void asm_movq_gpr_from_xmm(Assembler *a, int gpr, int xmm) {
    uint8_t rex = 0x48 | (gpr >= 8 ? 1 : 0) | (xmm >= 8 ? 4 : 0);
    asm_emit4(a, 0x66, rex, 0x0F, 0x7E);
    asm_emit1(a, (uint8_t)(0xC0 | ((xmm & 7) << 3) | (gpr & 7)));
}

/* =========================================================================
 * codegen_expr — evaluate expr, result in RAX/EAX
 * ========================================================================= */
void codegen_expr(CodeGen *cg, ASTNode *n) {
    Assembler *a=cg->asm_;
    if (!n) { asm_mov_reg_imm(a,REG_RAX,0); return; }
    /* Float dispatch: route float arithmetic through SSE2/x87 codegen.
     * Only pure-value nodes: AST_FLOAT literal, AST_BINARY, AST_UNARY, AST_CAST.
     * AST_ASSIGN and AST_VAR are handled below to avoid mutual recursion.
     * EXCLUDE "++"/"--" specifically: codegen_is_float_expr()'s own
     * AST_UNARY case reports true for ANY unary op whose operand is float
     * (op-agnostic — see its own case), so "++i"/"i--" on a float variable
     * used to get redirected here into codegen_float_expr() — which has NO
     * AST_UNARY case of its own (it only handles pure-value expressions,
     * not lvalue read-modify-write) and so silently did nothing, leaving
     * the variable's value completely unchanged. The switch below (this
     * function's own AST_UNARY case) has a dedicated, correct float
     * increment/decrement implementation — let "++"/"--" fall through to
     * it instead of being intercepted here. Confirmed as the real cause of
     * real SDL3's BesselI0() (SDL_audioresample.c) never advancing its
     * float loop index, diverging its series to +infinity instead of
     * converging (a genuine infinite loop) — a minimal repro ("float
     * i=1.0f; ++i;") showed i unchanged after any number of increments. */
    if ((n->kind==AST_FLOAT || n->kind==AST_BINARY || n->kind==AST_CAST ||
         (n->kind==AST_UNARY && strcmp(n->unary.op,"++")!=0 && strcmp(n->unary.op,"--")!=0))
        && codegen_is_float_expr(cg,n)) {
        codegen_float_expr(cg,n);
        /* Move float result to RAX as raw bits for comparison/branch context.
         * A bare AST_FLOAT literal used to return here WITHOUT this move (it
         * had its own early-return right above this block, before the
         * BINARY/UNARY/CAST cases' movq was added) — any caller treating
         * float args generically through codegen_expr (expecting the
         * result in RAX, not knowing/caring it's really a float) silently
         * got whatever garbage RAX last held instead of the literal's real
         * bits. This is exactly AST_FUNC_PTR_CALL's 5th-and-later
         * ("stack-passed") argument loop, which always uses codegen_expr
         * regardless of argument type — confirmed via SDL3's own
         * SDL_RenderTextureRotated -> renderer->QueueCopyEx(...) call (a
         * 10-argument function-pointer call with angle/scale_x/scale_y as
         * literal-or-computed float/double arguments past the first 4):
         * angle and both scale factors arrived as garbage, corrupting (and
         * in the real renderer, crashing on) every rotated-texture draw. */
        if (cg->is_64bit) { asm_emit4(a,0x66,0x48,0x0F,0x7E); asm_emit1(a,0xC0); }
        return;
    }

    switch (n->kind) {
    case AST_NUMBER:   asm_mov_reg_imm(a,REG_RAX,n->num.value); break;
    case AST_FLOAT:    codegen_float_expr(cg,n); return; /* handled above but fallback */
    case AST_CHAR_LIT: asm_mov_reg_imm(a,REG_RAX,n->char_lit.value); break;

    case AST_STRING: {
        const char *lbl=intern_string(cg,n->str.value,n->str.is_wide);
        if (cg->is_64bit) asm_lea_rip_data(a,REG_RAX,lbl);
        else { asm_emit1(a,0xB8); asm_reloc_data(a,lbl); }
        break;
    }

    case AST_VAR: {
        Symbol *s=symtable_lookup(cg->sym,n->var.name);
        if (!s) {
            /* The single most common real-world diagnostic squash produces
             * — almost always a typo, a missing #include, or a macro that
             * didn't expand the way the user expected. Give it the full
             * treatment: real file+line (not a raw line count into the
             * entire flattened translation unit), the enclosing function,
             * the actual source line with a caret under the identifier, and
             * a spelling suggestion from whatever's actually in scope. */
            char sugbuf[160] = "";
            const char *suggestion = diag_suggest_name(cg->sym, n->var.name);
            if (suggestion) snprintf(sugbuf, sizeof sugbuf, " (did you mean '%s'?)", suggestion);
            diag_emit(DIAG_ERROR, n->line, cg->cur_func_name, n->var.name,
                      "undefined identifier '%s'%s", n->var.name, sugbuf);
            asm_mov_reg_imm(a,REG_RAX,0); return;
        }
        if (s->kind==SYM_ENUM_VAL) { asm_mov_reg_imm(a,REG_RAX,s->enum_value); return; }
        if (s->kind==SYM_VAR||s->kind==SYM_PARAM) {
            if (s->array_size > 0) {
                /* Array decays to pointer-to-first-element: load address not value */
                asm_lea_rbp_disp(a, REG_RAX, s->offset);
            } else if (cg->is_64bit) {
                /* Use size-appropriate load to avoid reading garbage from adjacent stack slots.
                 * 1-byte vars: MOVZX EAX,byte[RBP+off] (zero-extends)
                 * 4-byte vars: MOV EAX,[RBP+off]       (zero-extends upper 32 bits of RAX)
                 * 8-byte vars: MOV RAX,[RBP+off]       (full 64-bit load)
                 * For signed char/short, use MOVSX for sign extension. */
                int vsz = s->type ? sizeof_type_sym(s->type, 1, cg->sym) : 4;
                /* Use TypeInfo.is_unsigned field: unsigned int/char/bool -> zero-extend,
                 * signed int/char/etc -> sign-extend for correct comparisons.
                 * type_is_unsigned_resolved() (not the raw is_unsigned flag)
                 * matters here: is_unsigned is only ever set when the
                 * declaration literally spells "unsigned ..."; a project
                 * typedef that merely RESOLVES to an unsigned type (e.g.
                 * SDL3's own "typedef Uint32 uint32_t;
                 * typedef unsigned int uint32_t;" chain — "Uint32 mask;"
                 * carries no literal "unsigned" token at its declaration
                 * site at all) left is_signed wrongly true here. That made
                 * every Uint32 local reload use MOVSXD (sign-extend) instead
                 * of a zero-extending load, and for any value >= 0x80000000
                 * (extremely ordinary for bitmasks — e.g. an alpha channel
                 * mask like 0xFF000000) this corrupts the upper 32 bits of
                 * the 64-bit register with all-1s. A subsequent 64-bit
                 * "shr" (used for real Uint32-typed loop counters like
                 * SDL_InitPixelFormatDetails()'s own "mask >>= 1") then
                 * leaks those corrupted 1-bits down into the low 32 bits
                 * once the shift count gets large enough, so the mask never
                 * reaches the state its loop's exit condition depends on —
                 * a genuine infinite loop, not just a wrong-value bug,
                 * confirmed via SDL_GetMasksForPixelFormat(ARGB8888)'s
                 * Amask=0xFF000000 hanging SDL_InitPixelFormatDetails()
                 * forever (real, unmodified SDL3 source). */
                int is_signed = !(s->type && type_is_unsigned_resolved(cg->sym, s->type));
                /* Note: plain "char" without unsigned qualifier is treated as signed
                 * (matches GCC default on x86). unsigned char uses MOVZX. */
                if (s->type && s->type->pointer_depth > 0) {
                    /* Pointer: full 64-bit load */
                    asm_mov_reg_mem(a, REG_RAX, REG_RBP, s->offset);
                } else if (vsz == 1) {
                    if (is_signed) {
                        /* MOVSX RAX, byte[RBP+off] — sign-extend 8-bit to 64-bit */
                        asm_movsx_rax_mem8(a, REG_RBP, s->offset);
                    } else {
                        asm_mov_eax_mem8(a, REG_RBP, s->offset);
                    }
                } else if (vsz <= 4) {
                    if (is_signed) {
                        /* MOVSXD RAX, dword[RBP+off] — sign-extend 32-bit to 64-bit */
                        asm_movsxd_rax_mem(a, REG_RBP, s->offset);
                    } else {
                        /* 32-bit load: zero-extends upper 32 bits of RAX */
                        asm_mov_reg32_mem(a, REG_RAX, REG_RBP, s->offset);
                    }
                } else {
                    /* 64-bit load (long long, double, pointer) */
                    asm_mov_reg_mem(a, REG_RAX, REG_RBP, s->offset);
                }
            } else {
                /* 32-bit: use size-appropriate load for chars */
                int vsz32 = s->type ? typeinfo_size(s->type, 0) : 4;
                int is_unsigned32 = (s->type && s->type->is_unsigned);
                if (vsz32 == 1) {
                    if (is_unsigned32) {
                        asm_movzx_eax_mem8(a, REG_EBP, s->offset); /* MOVZX EAX,byte[EBP+off] */
                    } else {
                        asm_movsx_eax_mem8(a, REG_EBP, s->offset); /* MOVSX EAX,byte[EBP+off] */
                    }
                } else {
                    asm_mov_reg_mem(a,REG_RAX,REG_RBP,s->offset);
                }
            }
        }
        else if (s->kind==SYM_GLOBAL) {
            /* Load value of a global variable — always in wdata (.data section). */
            const char *lbl = (s->dll && s->dll[0]) ? s->dll : n->var.name;
            int is_array = (s->array_size > 0);
            if (cg->is_64bit) {
                asm_lea_rip_wdata(a,REG_RAX,lbl);
                if (!is_array) {
                    int glsz = s->type ? sizeof_type_sym(s->type, 1, cg->sym) : 4;
                    int gl_is_ptr = s->type && s->type->pointer_depth > 0;
                    if (gl_is_ptr || glsz == 8)
                        asm_emit3(a,0x48,0x8B,0x00); /* mov rax,[rax] — 64-bit load */
                    else if (glsz == 1)
                        asm_emit3(a,0x0F,0xB6,0x00); /* movzx eax,byte[rax] */
                    else if (glsz == 2)
                        asm_emit4(a,0x66,0x0F,0xB7,0x00); /* movzx eax,word[rax] */
                    else
                        asm_emit2(a,0x8B,0x00); /* mov eax,[rax] — 32-bit load */
                }
            } else {
                if (!is_array) {
                    /* Read 32-bit global via EBX to avoid abs-addr heuristic */
                    asm_push_reg(a,REG_EBX);
                    asm_emit1(a,0xBB); asm_reloc_wdata(a,lbl); /* MOV EBX,&global */
                    asm_emit2(a,0x8B,0x03); /* MOV EAX,[EBX] */
                    asm_pop_reg(a,REG_EBX);
                } else {
                    asm_emit1(a,0xB8); asm_reloc_wdata(a,lbl); /* global in wdata */
                }
            }
        } else if (s->kind==SYM_FUNC) {
            /* Load function address into RAX — used when assigning to a
             * function pointer variable: int (*op)(int,int) = fp_add;
             * BUT if this name is actually a known Windows DLL export
             * (declared via an extern prototype with no body in this TU,
             * e.g. "_beginthreadex" from include/process.h, referenced by
             * name via SDL3's SDL_BeginThreadFunction macro rather than
             * called directly), there is no real local function LABEL to
             * take the address of — get_func_label()/asm_load_func_addr()
             * would silently emit a reference to a label that's never
             * defined anywhere, failing at link time with "asm_resolve:
             * undefined label". Load the DLL import's real address from
             * its IAT slot instead (same mechanism asm_call_import() uses
             * to CALL it, just reading the slot's value instead of jumping
             * through it). */
            const char *dllname = cg->is_linux ? NULL : symtable_find_dll(cg->sym, n->var.name);
            if (dllname) {
                char key[512]; snprintf(key,sizeof key,"%s:%s",dllname,n->var.name);
                symtable_add_import(cg->sym,key);
                asm_load_import_addr(a, REG_RAX, n->var.name);
            } else if (s->func_node == NULL || s->func_node->func.body == NULL) {
                /* Declared (prototype seen — e.g. via an #include'd header)
                 * but with no body anywhere in THIS translation unit, and
                 * not a recognized Win32 DLL export — same "separate
                 * compilation" gap as the SYM_IMPORT case just below and the
                 * call-site fixes above: get_func_label()/asm_load_func_addr()
                 * would reference a local label that's never defined in this
                 * TU, failing asm_resolve() with "undefined label" (e.g.
                 * codegen.c's own `fn = asm_add_reg_reg;` function-pointer
                 * table, where asm_add_reg_reg is defined in assembler.c, a
                 * sibling .sqo not yet linked when codegen.c is compiled on
                 * its own). Load it the same way asm_call_static() calls it:
                 * a named, PC-relative deferred reference the object-file
                 * linker resolves later by name. */
                asm_lea_rip_static(a, REG_RAX, n->var.name);
            } else {
                int func_lbl = get_func_label(cg, n->var.name);
                asm_load_func_addr(a, REG_RAX, func_lbl);
            }
        } else if (s->kind==SYM_IMPORT) {
            /* Bare reference to a function's NAME (not a call) whose
             * declaration was bodyless-"extern" — e.g. real SDL3 code
             * passing a cross-object callback BY NAME, such as
             * "SDL_CreateHashTable(0, true, SDL_HashID, SDL_KeyMatchID,
             * SDL_DestroyHashValue, NULL)" from a video/audio-part object
             * where those three callbacks are actually DEFINED in the core
             * part. parser_new4.c's ParseFunction registers EVERY bodyless
             * "extern RET name(...)" as SYM_IMPORT with dll="extern" (see
             * the identical call-site fix above and in codegen's argument-
             * marshaling code) — before this fix, this fell through to the
             * final catch-all "else" below and silently loaded 0 instead of
             * the callback's real address, so the callee received a NULL
             * function pointer with no error anywhere (confirmed via the
             * SDL3_Build video-subsystem-split crash: SDL_format_details's
             * hash table ended up with hash/keymatch/destroy all NULL,
             * crashing the first time anything actually called through
             * one). Mirrors the call-site fix: check for a genuine Windows
             * DLL export first (EXTERN_C is literally "extern" in C mode,
             * so a real API function's address-taken reference is
             * indistinguishable from this placeholder at parse time), else
             * treat it as this compiler's own cross-object symbol and load
             * its address via the same RELOC_STATIC_REL32 mechanism
             * asm_call_static() uses to call it. */
            if (s->dll && strcmp(s->dll,"extern")==0) {
                const char *realdll = cg->is_linux ? NULL : symtable_find_dll(cg->sym, n->var.name);
                if (realdll) {
                    char key3[512]; snprintf(key3,sizeof key3,"%s:%s",realdll,n->var.name);
                    symtable_add_import(cg->sym,key3);
                    asm_load_import_addr(a, REG_RAX, n->var.name);
                } else {
                    asm_lea_rip_static(a, REG_RAX, n->var.name);
                }
            } else {
                char key4[512]; snprintf(key4,sizeof key4,"%s:%s",s->dll,n->var.name);
                symtable_add_import(cg->sym,key4);
                asm_load_import_addr(a, REG_RAX, n->var.name);
            }
        } else {
            asm_mov_reg_imm(a,REG_RAX,0);
        }
        break;
    }

    case AST_SIZEOF_TYPE:
        asm_mov_reg_imm(a,REG_RAX,(long long)sizeof_type_sym(n->sizeof_type.type,cg->is_64bit,cg->sym));
        break;
    case AST_SIZEOF_EXPR: {
        /* Look up the size of the expression's type.
         * For arrays: return the full array byte size.
         * For pointers/scalars: return the pointer or type size. */
        ASTNode *se = n->sizeof_expr.expr;
        /* "*(&expr)" (dereference of an address-of) is algebraically just
         * "expr" — the address-of/dereference cancel out — but this exact
         * shape didn't match any of the cases below (only a bare AST_VAR
         * pointer dereference did), so it silently fell through to the
         * pointer-size default. This is NOT a rare pattern: it's exactly
         * what pointer-generic C macros expand to when called on the
         * address of a local, e.g. SDL3's own "#define SDL_zerop(x)
         * SDL_memset((x), 0, sizeof(*(x)))" invoked as "SDL_zerop(&iface)"
         * — sizeof(*(&iface)) silently came out as 8 (pointer size)
         * instead of iface's real size, so SDL_INIT_INTERFACE's "version =
         * sizeof(*(iface))" (same macro pattern) stamped a bogus, too-small
         * version number into every SDL_IOStreamInterface, which then
         * failed its own "version < sizeof(*iface)" validity check —
         * confirmed as the actual root cause of a multi-session
         * SDL_iostream.c investigation. Unwrap any number of "*(&...)"
         * layers down to the innermost real expression before applying the
         * existing per-shape logic below. */
        while (se && se->kind == AST_DEREF && se->deref.operand &&
               se->deref.operand->kind == AST_ADDR && se->deref.operand->addr.operand) {
            se = se->deref.operand->addr.operand;
        }
        long long sz = cg->is_64bit ? 8 : 4; /* default: pointer size */
        if (se && se->kind == AST_VAR) {
            Symbol *esym = symtable_lookup(cg->sym, se->var.name);
            if (esym && esym->type) {
                int sym_arr = esym->array_size;
                /* If symbol-level array_size differs from type-level (typedef resolution),
                 * temporarily patch the type so sizeof_type_sym gets the right count. */
                int old_arr = esym->type->array_size;
                if (sym_arr > 0 && esym->type->array_size != sym_arr)
                    esym->type->array_size = sym_arr;
                sz = (long long)sizeof_type_sym(esym->type, cg->is_64bit, cg->sym);
                esym->type->array_size = old_arr;  /* restore */
            }
        } else if (se && se->kind == AST_DEREF && se->deref.operand &&
                   se->deref.operand->kind == AST_VAR) {
            /* sizeof *ptr — compute size of the pointed-to type */
            Symbol *esym = symtable_lookup(cg->sym, se->deref.operand->var.name);
            if (esym && esym->type && esym->type->pointer_depth > 0) {
                TypeInfo tmp;
                tmp.base          = esym->type->base;
                tmp.pointer_depth = esym->type->pointer_depth - 1;
                tmp.array_size    = esym->type->array_size;
                sz = (long long)sizeof_type_sym(&tmp, cg->is_64bit, cg->sym);
            }
        } else if (se && se->kind == AST_DEREF && se->deref.operand &&
                   se->deref.operand->kind == AST_MEMBER) {
            /* sizeof(*obj->ptr_field) / sizeof(*obj.ptr_field) — dereferencing
             * a POINTER-TYPED STRUCT FIELD, e.g. the extremely common
             * "obj = SDL_calloc(1, sizeof(*obj->hidden));" idiom (real SDL3
             * code, e.g. this project's own PRIVATEAUDIO_OpenDevice audio
             * backend: "device->hidden = SDL_calloc(1, sizeof(*device->hidden));").
             * Same class of gap as the AST_VAR case just above (only a bare
             * pointer VARIABLE dereference was handled, not a pointer FIELD
             * accessed via ./-> ), silently mis-sizing to the pointer-size
             * default (8) instead of the real pointed-to struct's size —
             * this under-allocates the buffer by however much the real
             * struct is bigger than a pointer, and every field access past
             * the first pointer-sized slot then reads/writes uninitialized
             * heap memory one struct-body's-worth beyond the too-small
             * allocation. Confirmed via WAVEHDR.dwFlags reading heap garbage
             * instead of the calloc-zeroed 0, which made waveOutPrepareHeader
             * intermittently fail with MMSYSERR_INVALPARAM — the actual root
             * cause of squash-built SDL3 audio examples playing no sound. */
            TypeInfo *mft = field_type_of(cg, se->deref.operand->member.obj, se->deref.operand->member.field);
            if (mft && mft->pointer_depth > 0) {
                TypeInfo tmp;
                tmp.base          = mft->base;
                tmp.pointer_depth = mft->pointer_depth - 1;
                tmp.array_size    = -1;
                sz = (long long)sizeof_type_sym(&tmp, cg->is_64bit, cg->sym);
            }
        } else if (se && se->kind == AST_INDEX && se->index.array) {
            /* sizeof(arr[i]) — e.g. SDL3's own SDL_arraysize(a) macro,
             * "sizeof(a)/sizeof(a[0])". Previously fell through to the
             * pointer-size default (8 bytes) for anything but a bare
             * variable/deref, silently mis-sizing SDL_arraysize whenever
             * the element type wasn't exactly pointer-sized — e.g.
             * SDL_iconv.c's "encodings[]" (16-byte struct elements) made
             * SDL_arraysize(encodings) come out to 116 instead of 58,
             * so SDL_iconv_open()'s scan ran off the end of the real
             * array into whatever followed it, eventually dereferencing
             * garbage as a string pointer — a genuine
             * STATUS_ACCESS_VIOLATION, not an SDL bug. elem_size_of()
             * already correctly resolves struct/array element sizes
             * (used for index-address arithmetic elsewhere), so reuse it
             * here instead of duplicating that logic. */
            sz = (long long)elem_size_of(cg, se->index.array);
        } else if (se && se->kind == AST_MEMBER) {
            /* sizeof(obj.field) / sizeof(obj->field) — e.g. SDL3's own
             * SDL_copyp(dst, src) macro's compile-time-assert condition
             * "sizeof(*(dst)) == sizeof(*(src))", called as
             * "SDL_copyp(&storage->iface, iface)": squash defines
             * __STDC_VERSION__ as C17, so SDL_COMPILE_TIME_ASSERT expands
             * to a real "_Static_assert(cond, msg)", which squash's parser
             * compiles as a genuine (if discarded) runtime expression
             * statement — so this sizeof is actually evaluated, not just
             * parsed and ignored. "sizeof(*(&storage->iface))" unwraps (via
             * the loop above) to "sizeof(storage->iface)", an AST_MEMBER
             * with no case here previously, silently mis-sizing to the
             * pointer-size default. Harmless for the assert itself (its
             * result is unused), but the exact same "sizeof(*(&member))"
             * shape recurs anywhere a pointer-generic macro is invoked on
             * the address of a struct FIELD instead of a plain variable —
             * matches the class of bug already fixed for AST_VAR. */
            TypeInfo *mft = field_type_of(cg, se->member.obj, se->member.field);
            if (mft && mft->pointer_depth == 0) {
                sz = (long long)sizeof_type_sym(mft, cg->is_64bit, cg->sym);
                /* sizeof_type_sym(mft,...) alone gives just ONE element's
                 * size for an array-typed field -- see
                 * field_array_size_of()'s own comment on why the true
                 * element count can't be read off mft->array_size itself. */
                int arr = field_array_size_of(cg, se->member.obj, se->member.field);
                if (arr > 0) sz *= arr;
            } else if (mft) {
                sz = cg->is_64bit ? 8 : 4; /* pointer-typed field */
            }
        }
        asm_mov_reg_imm(a, REG_RAX, sz);
        break;
    }

    case AST_CAST: {
        /* Check if we are casting a float expression to an integer type.
         * If so, use proper float->int conversion (cvttsd2si / fistp),
         * NOT the raw-bit movq path that codegen_expr uses for float exprs. */
        int inner_is_float = codegen_is_float_expr(cg, n->cast.expr);
        int target_is_float = n->cast.type && is_float_type(cg, n->cast.type);
        if (inner_is_float && !target_is_float) {
            /* float expr -> integer target: evaluate float then convert */
            codegen_float_expr(cg, n->cast.expr);
            if (cg->is_64bit) {
                /* cvttsd2si eax,xmm0  (truncate toward zero) */
                asm_emit4(a, 0xF2, 0x0F, 0x2C, 0xC0);
                /* movsxd rax,eax — sign-extend int32 result to int64 */
                asm_emit3(a, 0x48, 0x63, 0xC0);
            } else {
                /* 32-bit float->int using x87 fnstcw/fldcw/fistp (no SSE2 required).
                 * We need extra stack space: 2 bytes for saved CW, 2 for truncating CW,
                 * 4 bytes for the fistp result.
                 * Allocate 8 bytes total so ESP stays aligned.
                 *   [esp+0..1] = result (fistp dword)
                 *   [esp+2..3] = modified CW (truncation mode)
                 *   [esp+4..5] = original CW (fnstcw)
                 *   [esp+6..7] = padding
                 */
                asm_emit3(a, 0x83, 0xEC, 0x08);             /* sub esp,8           */
                asm_emit3(a, 0x9B, 0xD9, 0x7C); asm_emit1(a, 0x24); asm_emit1(a, 0x04); /* fnstcw [esp+4] */
                asm_emit4(a, 0x0F, 0xB7, 0x44, 0x24); asm_emit1(a, 0x04); /* movzx eax,word[esp+4] */
                asm_emit1(a, 0x0D); asm_emit_u32(a, 0x0C00); /* or eax,0x0C00 (set RC=truncate) */
                asm_emit3(a, 0x66, 0x89, 0x44); asm_emit1(a, 0x24); asm_emit1(a, 0x02); /* mov [esp+2],ax */
                asm_emit3(a, 0xD9, 0x6C, 0x24); asm_emit1(a, 0x02); /* fldcw [esp+2] */
                asm_emit3(a, 0xDB, 0x1C, 0x24); /* fistp dword[esp] (truncate ST0->int32, pop) */
                asm_emit3(a, 0xD9, 0x6C, 0x24); asm_emit1(a, 0x04); /* fldcw [esp+4] (restore) */
                asm_emit3(a, 0x8B, 0x04, 0x24); /* mov eax,[esp] */
                asm_emit3(a, 0x83, 0xC4, 0x08);             /* add esp,8           */
            }
            break;
        }
        if (inner_is_float && target_is_float) {
            /* float->float cast (e.g. double->float): evaluate as float */
            codegen_float_expr(cg, n->cast.expr);
            if (cg->is_64bit) {
                /* cvtsd2ss xmm0,xmm0 if narrowing, else no-op for widening */
                TypeInfo *tt = n->cast.type;
                if (tt && tt->base && strcmp(tt->base,"float")==0)
                    asm_emit4(a, 0xF2, 0x0F, 0x5A, 0xC0); /* cvtsd2ss xmm0,xmm0 */
            }
            break;
        }
        codegen_expr(cg, n->cast.expr);
        /* Apply narrowing casts by truncating and sign/zero extending.
         * Without this, (char)0x1FF stays 0x1FF instead of becoming -1.
         * Rules (mirrors C integer conversion):
         *   (char)  / (signed char)  -> movsx eax,al   sign-extend low byte
         *   (unsigned char)          -> movzx eax,al   zero-extend low byte
         *   (short) / (signed short) -> movsx eax,ax   sign-extend low word
         *   (unsigned short)         -> movzx eax,ax
         *   (int)/(long)/(ptr)/etc.  -> no-op
         */
        if (n->cast.type) {
            const char *base = n->cast.type->base;
            int is_unsigned  = n->cast.type->is_unsigned;
            int pdepth       = n->cast.type->pointer_depth;
            if (pdepth == 0) {
                /* ParseTypeSpecifier() (parser_new4.c) stores "unsigned" as
                 * a separate is_unsigned=1 flag, NOT folded into base as
                 * the combined string "unsigned char"/"unsigned short" --
                 * base is always just "char"/"short" regardless of
                 * signedness. The strcmp(base,"unsigned char"/"unsigned
                 * short") checks below this comment used to NEVER match
                 * anything for exactly that reason, so an "(unsigned
                 * char)expr" cast silently emitted no mask/extend
                 * instruction at all -- whatever sign-extended garbage
                 * codegen_expr(n->cast.expr) already left in eax (e.g. a
                 * plain signed `char` array/pointer read, movsx'd) passed
                 * straight through unmodified. Confirmed via a minimal
                 * repro: "(unsigned char)body[0]" for a char* body holding
                 * byte 0xD8 (216) came back as -40 (the SIGNED
                 * interpretation) instead of 216 -- the real cause of
                 * SQW's own JPEG magic-byte sniff ("body[0]==0xFF") never
                 * matching a real JPEG response body. Check is_unsigned
                 * alongside the bare "char"/"short" base name instead of
                 * ever expecting the combined string. */
                if (is_unsigned && strcmp(base,"char")==0) {
                    /* movzx eax,al */
                    asm_emit3(a,0x0F,0xB6,0xC0);
                } else if (strcmp(base,"char")==0 || strcmp(base,"signed char")==0) {
                    /* movsx eax,al */
                    if (cg->is_64bit) asm_emit4(a,0x48,0x0F,0xBE,0xC0);
                    else              asm_emit3(a,0x0F,0xBE,0xC0);
                } else if (strcmp(base,"unsigned char")==0) {
                    /* movzx eax,al -- kept for a hypothetical future parser
                     * that DOES fold the combined string into base. */
                    asm_emit3(a,0x0F,0xB6,0xC0);
                } else if (is_unsigned && strcmp(base,"short")==0) {
                    /* movzx eax,ax */
                    asm_emit3(a,0x0F,0xB7,0xC0);
                } else if (strcmp(base,"short")==0 || strcmp(base,"signed short")==0) {
                    /* movsx eax,ax */
                    if (cg->is_64bit) asm_emit4(a,0x48,0x0F,0xBF,0xC0);
                    else              asm_emit3(a,0x0F,0xBF,0xC0);
                } else if (strcmp(base,"unsigned short")==0) {
                    /* movzx eax,ax -- same "hypothetical combined string" note as above. */
                    asm_emit3(a,0x0F,0xB7,0xC0);
                }
                /* int/long/void* etc: value already correct width in eax/rax */
            }
        }
        break;
    } /* end case AST_CAST */

    case AST_ADDR:
        codegen_lvalue(cg,n->addr.operand);
        break;

    case AST_DEREF: {
        /* Dereference pointer; load size depends on pointed-to type. */
        int load1=0, load2=0, load8=0;
        /* Signedness of the pointee, for the plain-4-byte-int case at the
         * bottom of this block — see that code's own comment for why this
         * matters (a real, confirmed bug: a NEGATIVE `int` read through
         * `*ptr` came back as a huge POSITIVE value once later widened to
         * 64-bit, e.g. by (float)(*intPtr) or intPtr's own value compared/
         * used in a 64-bit context). Defaults to signed (0) — only set to 1
         * when the pointee's TypeInfo explicitly says unsigned. */
        int deref_is_unsigned = 0;
        /* Determine load size from the operand's symbol type. The operand
         * is often not a bare variable — e.g. "*string++" (SDL_strlen's own
         * loop) wraps the variable in a postfix-++ AST_UNARY node, and
         * pointer arithmetic like "*(p+1)" wraps it in an AST_BINARY. Walk
         * down through these to find the real underlying variable, since
         * without this the code fell through to a default 4-byte load
         * regardless of the pointee type — e.g. dereferencing a char*
         * through "*p++" read 4 bytes instead of 1, so a strlen-style loop
         * scanned in the wrong stride and only stopped once it happened to
         * find 4 consecutive zero bytes, running past the real NUL
         * terminator into whatever adjacent memory followed. */
        /* An explicit cast — e.g. "*(const char**)ptr_expr" (this is
         * exactly how va_arg(ap,type) expands: "(*(type*)((ap+=..)-..))")
         * — is the AUTHORITATIVE type of what's being dereferenced and
         * must be checked before falling back to inferring a type from
         * some underlying variable. Without this, the walk below doesn't
         * know how to look through AST_CAST, gives up (deref_var=NULL),
         * and silently fell through to a default 32-bit load — so
         * va_arg(ap, char*) (or any 8-byte pointer/type) truncated its
         * result to the low 4 bytes instead of reading the full pointer,
         * corrupting every variadic pointer argument after the first. */
        TypeInfo *cast_ty = (n->deref.operand && n->deref.operand->kind==AST_CAST)
                             ? n->deref.operand->cast.type : NULL;
        if (cast_ty && cast_ty->pointer_depth>=1) {
            const char *cbase=cast_ty->base;
            if (cast_ty->pointer_depth>=2) load8=cg->is_64bit;
            else if (cbase && (strcmp(cbase,"char")==0||strcmp(cbase,"signed char")==0||
                         strcmp(cbase,"unsigned char")==0)) load1=1;
            else if (cbase && (strcmp(cbase,"short")==0||strcmp(cbase,"unsigned short")==0)) load2=1;
            else if (cbase) {
                /* Single level of pointer, pointee neither char nor short —
                 * e.g. va_arg(ap, unsigned long long)/va_arg(ap, long long)/
                 * va_arg(ap, double)/va_arg(ap, Uint64) all expand to
                 * "*(unsigned long long*)(...)"-shaped casts, giving
                 * cast_ty->pointer_depth==1 with cbase=="unsigned long
                 * long" (or similar) — neither char nor short, and this
                 * whole if/else-if chain previously had NO other case for
                 * pointer_depth==1, so it fell all the way through to the
                 * generic 4-byte-load default below, silently truncating
                 * every va_arg() read of a plain (non-char/short) 8-byte
                 * scalar type to its low 32 bits. Check the POINTEE type's
                 * own size (pointer_depth 0) to decide 4 vs 8 bytes, same
                 * as any other scalar. */
                TypeInfo tmp;
                tmp.base=cbase; tmp.pointer_depth=0; tmp.is_const=cast_ty->is_const;
                tmp.is_unsigned=cast_ty->is_unsigned; tmp.array_size=-1; tmp.array_size2=0;
                tmp.pointed_to=cast_ty->pointed_to; tmp.is_volatile=cast_ty->is_volatile;
                tmp.is_inline=cast_ty->is_inline; tmp.is_extern=cast_ty->is_extern;
                tmp.is_float=cast_ty->is_float;
                if (sizeof_type_sym(&tmp,cg->is_64bit,cg->sym)==8) load8=cg->is_64bit;
                deref_is_unsigned = cast_ty->is_unsigned;
            }
        } else if (cast_ty && cast_ty->pointer_depth==0 &&
                   (sizeof_type_sym(cast_ty,cg->is_64bit,cg->sym)==8)) {
            /* e.g. va_arg(ap,double)/va_arg(ap,long long): 8-byte scalar cast */
            load8=cg->is_64bit;
        }
        ASTNode *deref_var = n->deref.operand;
        while (!cast_ty && deref_var && deref_var->kind != AST_VAR) {
            if (deref_var->kind==AST_UNARY && deref_var->unary.operand)
                deref_var = deref_var->unary.operand;
            else if (deref_var->kind==AST_BINARY && deref_var->binary.left)
                deref_var = deref_var->binary.left;
            else if (deref_var->kind==AST_ASSIGN && deref_var->assign.lhs)
                deref_var = deref_var->assign.lhs;
            else { deref_var = NULL; break; }
        }
        if (!cast_ty && deref_var && deref_var->kind==AST_VAR) {
            Symbol *ds=symtable_lookup(cg->sym,deref_var->var.name);
            if (ds && ds->type && ds->type->pointer_depth>=1) {
                const char *base=ds->type->base;
                /* pointer_depth>=2 (e.g. "const char **_str") must ALWAYS
                 * use the 8-byte pointer load, regardless of base type —
                 * dereferencing ONE level of a multi-level pointer yields
                 * ANOTHER POINTER, not a scalar of the base type. The
                 * base=="char"/"short" checks below only apply at the
                 * final level of indirection (pointer_depth==1); checking
                 * base before pointer_depth let "const char **_str" (e.g.
                 * SDL3's own StepUTF8) incorrectly take a 1-byte load for
                 * "*_str" (base=="char"), loading a tiny garbage value
                 * instead of the real 8-byte char* — using that as a
                 * pointer moments later was a real STATUS_ACCESS_VIOLATION,
                 * not an SDL bug. */
                if (ds->type->pointer_depth>=2) {
                    load8=cg->is_64bit;
                } else if (base && (strcmp(base,"char")==0||strcmp(base,"signed char")==0||
                             strcmp(base,"unsigned char")==0)) load1=1;
                else if (base && (strcmp(base,"short")==0||strcmp(base,"unsigned short")==0)) load2=1;
                else if (base && is_byte_sized_stdint(cg->sym,base)) load1=1;
                else if (base && is_short_sized_stdint(cg->sym,base)) load2=1;
                else deref_is_unsigned = ds->type->is_unsigned;
            }
        }
        codegen_expr(cg,n->deref.operand);
        if (load1) {
            if (cg->is_64bit) asm_emit3(a,0x0F,0xB6,0x00); /* movzx eax,byte[rax] */
            else              asm_emit3(a,0x0F,0xB6,0x00); /* movzx eax,byte[eax] */
        } else if (load2) {
            asm_emit3(a,0x0F,0xB7,0x00); /* movzx eax,word[rax] */
        } else if (load8 && cg->is_64bit) {
            asm_emit3(a,0x48,0x8B,0x00); /* mov rax,[rax] 64-bit */
        } else {
            /* Plain 4-byte int pointee: a SIGNED int (the common case --
             * "int *p; ...; *p") must sign-extend into the full 64-bit
             * RAX, not just zero-extend via mov eax,[rax] -- otherwise a
             * negative value's upper 32 bits read back as 0 instead of
             * all-1s, and anything downstream that treats RAX as a real
             * 64-bit signed quantity (e.g. asm_cvtsi2sd() in 64-bit mode,
             * which ALWAYS takes a REX.W/64-bit GPR source, see its own
             * comment) sees a huge positive number instead of the real
             * negative value. Confirmed via a minimal repro:
             * "(float)(*intPtr)" for a negative *intPtr came back as
             * roughly +4.3 billion instead of the real negative value --
             * this is what corrupted every negative JPEG DC coefficient
             * diff in SQW's own baseline decoder (jpeg_decode_block's
             * "*dc_pred" -- see img_decode_jpeg.c). Zero-extension (movzx-
             * equivalent mov eax,[rax], the previous unconditional
             * behavior) remains correct and is kept for a genuinely
             * unsigned int pointee, where the upper bits should read as 0
             * -- same signed-vs-unsigned distinction AST_INDEX's own
             * array-element load already makes just above (see its
             * comment), now applied here too. */
            if (!cg->is_64bit) asm_emit2(a,0x8B,0x00); /* mov eax,[eax] */
            else if (deref_is_unsigned) asm_emit2(a,0x8B,0x00); /* mov eax,[rax] zero-extends */
            else asm_emit3(a,0x48,0x63,0x00); /* movsxd rax,dword[rax] sign-extends */
        }
        break;
    }

    case AST_INDEX: {
        if (!n->index.array || !n->index.index) { asm_mov_reg_imm(a,REG_RAX,0); break; }
        /* T x[N][M]: x[i] yields row i, which is itself an array — decays
         * to a pointer (address left in RAX/EAX by codegen_lvalue below),
         * matching plain array-to-pointer decay. Only x[i][j] (a further
         * AST_INDEX wrapping this one) actually loads a scalar. */
        int base_is_2d = 0;
        if (n->index.array->kind == AST_VAR) {
            Symbol *bsym = symtable_lookup(cg->sym, n->index.array->var.name);
            base_is_2d = (bsym && bsym->array_size2 > 0);
        } else if (n->index.array->kind == AST_MEMBER) {
            int dummy;
            field_scalar_size(cg, n->index.array->member.obj, n->index.array->member.field, &dummy);
            base_is_2d = dummy;
        }
        if (base_is_2d) {
            codegen_lvalue(cg, n);
            break;
        }
        int esz = elem_size_of(cg, n->index.array);
        codegen_lvalue(cg, n);          /* RAX/EAX = address of element */
        /* Load element using appropriate width.
         *
         * A signed element narrower than the 64-bit register it lands in
         * MUST be sign-extended, not zero-extended, whenever the target is
         * 64-bit: a local variable read of the same underlying C type
         * already goes through a sign-extending load elsewhere in this
         * file (e.g. "movslq" for a signed int local), so a negative
         * value's in-register 64-bit representation is all-1s in the
         * upper bits. A zero-extending array-element load instead leaves
         * those upper bits 0 -- two representations of the identical C
         * value that differ as raw 64-bit bit patterns. Nothing notices
         * until the two meet: a subsequent 64-bit comparison/arithmetic op
         * against ANYTHING sign-extended (a negative literal, a local var
         * read) sees them as unequal, even though the underlying 32/16/8-
         * bit int values genuinely are equal. Confirmed via a minimal
         * repro: "int arr[10]; ...; arr[i] != -1" evaluated true even when
         * arr[i] really was -1 (identical bug for signed char/short
         * arrays: "carr[i] != -1"/"sarr[i] != -1"). Zero-extension remains
         * correct — and is kept — for unsigned element types, where the
         * upper bits genuinely should read as 0. */
        int is_uns = expr_is_unsigned_int(cg, n->index.array);
        if (esz == 8 && cg->is_64bit) {
            asm_emit3(a,0x48,0x8B,0x00); /* mov rax,[rax]          64-bit     */
        } else if (esz == 1) {
            if (cg->is_64bit && !is_uns) asm_emit4(a,0x48,0x0F,0xBE,0x00); /* movsx rax,byte[rax] */
            else                         asm_emit3(a,0x0F,0xB6,0x00);     /* movzx eax,byte[rax] */
        } else if (esz == 2) {
            if (cg->is_64bit && !is_uns) asm_emit4(a,0x48,0x0F,0xBF,0x00); /* movsx rax,word[rax] */
            else                         asm_emit3(a,0x0F,0xB7,0x00);     /* movzx eax,word[rax] */
        } else {
            if (cg->is_64bit && !is_uns) asm_emit3(a,0x48,0x63,0x00);     /* movsxd rax,dword[rax] */
            else                         asm_emit2(a,0x8B,0x00);         /* mov eax,[rax] (32-bit) */
        }
        break;
    }

    case AST_MEMBER: {
        /* Get base address of struct into RAX/EAX, then load field value */
        if (n->member.arrow) codegen_expr(cg,n->member.obj);
        else codegen_lvalue(cg,n->member.obj);
        { int foff = field_byte_offset(cg->sym, n->member.obj, n->member.field);
          /* Determine load width from field type */
          int load64 = 0;
          int is_array_field = 0; /* fixed-size array: return address, not value */
          int field_is_unsigned = 0; /* track for sign extension */
          int field_sz = 4; /* default field byte size */
          int field_found = 0; /* whether we successfully resolved the field type */
          /* Field type resolution — needed in both 32-bit and 64-bit modes */
          {
              ASTNode *mobj = n->member.obj;
              const char *mfname = n->member.field;
              const char *mstype = NULL;
              if (mobj->kind == AST_VAR) {
                  Symbol *sv = symtable_lookup(cg->sym, mobj->var.name);
                  if (sv && sv->type) mstype = sv->type->base;
              } else if (mobj->kind == AST_DEREF || n->member.arrow) {
                  ASTNode *op = (mobj->kind==AST_DEREF) ? mobj->deref.operand : mobj;
                  if (op->kind == AST_VAR) {
                      Symbol *sv = symtable_lookup(cg->sym, op->var.name);
                      if (sv && sv->type) mstype = sv->type->base;
                  } else if (op->kind == AST_MEMBER || op->kind == AST_INDEX) {
                      /* chained arrow: (expr)->field where expr is not a simple var */
                      mstype = resolve_node_type(cg->sym, op);
                  } else if (op->kind == AST_CAST && op->cast.type) {
                      /* ((T*)expr)->field — an explicit cast right before the
                       * arrow (e.g. COM-style "((Foo*)p)->lpVtbl->Method()")
                       * names the pointee type directly, so use it instead of
                       * trying to trace back to some declared variable's type
                       * (which this resolution can't do through an arbitrary
                       * cast — same root cause already fixed for the STORE
                       * side's "*(T*)expr = val" pattern above; this is the
                       * READ/LOAD-side twin of that bug). Without this,
                       * field_found stayed 0 and load64 stayed 0, so ANY
                       * pointer- or function-pointer-typed field read through
                       * a cast base silently used the 4-byte zero-extending
                       * load instead of the real 8-byte one — confirmed via
                       * a minimal repro: a vtable-style function pointer
                       * field read as "(Foo*)p)->GetValue" had its top 32
                       * bits zeroed, then calling through the truncated
                       * "pointer" crashed. */
                      mstype = op->cast.type->base;
                  }
              } else if (mobj->kind == AST_MEMBER || mobj->kind == AST_INDEX) {
                  mstype = resolve_node_type(cg->sym, mobj);
              }
              if (mstype) {
                  const char *bare = mstype;
                  if (strncmp(bare,"struct ",7)==0) bare+=7;
                  else if (strncmp(bare,"union ",6)==0) bare+=6;
                  char sk[256]; snprintf(sk,sizeof sk,"struct %s",bare);
                  Symbol *ss = symtable_lookup(cg->sym, sk);
                  if (!ss) {
                      /* typedef resolution: e.g. CodeGen/Assembler/SymTable -> struct $anonN */
                      Symbol *td = symtable_lookup(cg->sym, mstype);
                      if (td && td->kind==SYM_TYPEDEF && td->type) {
                          const char *tb = td->type->base;
                          if (strncmp(tb,"struct ",7)==0) tb+=7;
                          else if (strncmp(tb,"union ",6)==0) tb+=6;
                          snprintf(sk,sizeof sk,"struct %s",tb);
                          ss = symtable_lookup(cg->sym, sk);
                      }
                  }
                  if (ss && ss->struct_node) {
                      for (int _i=0;_i<ss->struct_node->struct_decl.nfields;_i++) {
                          ASTNode *ff=ss->struct_node->struct_decl.fields[_i];
                          if (ff&&ff->field.name&&strcmp(ff->field.name,mfname)==0&&ff->field.type) {
                              /* Resolve typedef'd field types before checking pointer_depth —
                               * a field declared as "PFNGLXFOOPROC f;" (function-pointer
                               * typedef) has pointer_depth==0 on its OWN TypeInfo (the "*"
                               * lives on the typedef's underlying type instead), so without
                               * this it was misread as a plain 4-byte int field and loaded
                               * with movsxd (sign-extend), corrupting the pointer's upper bits.
                               * MUST loop, not just resolve one level: real SDL3 has genuine
                               * typedef-CHAINS, e.g. "typedef SDL_AudioStreamDataCompleteCallback
                               * SDL_ReleaseAudioBufferCallback;" (SDL_audioqueue.h) where THAT
                               * name is itself only resolved to the real "void(*)(...)" function
                               * pointer by a SECOND typedef lookup — a single `if` here stops
                               * after the first hop, still sees pointer_depth==0, and silently
                               * keeps the 32-bit sign-extending load. Confirmed as the real cause
                               * of a STATUS_ACCESS_VIOLATION calling through
                               * SDL_AudioTrack::callback once real audio data finally reached
                               * DestroyAudioTrack() for the first time (the upper 32 bits of the
                               * callback's real address were lost on load, and the low 32 bits
                               * alone pointed at unmapped memory). */
                              TypeInfo *fty = ff->field.type;
                              for (int _td_hops = 0; _td_hops < 8 && fty->pointer_depth==0 && fty->base; _td_hops++) {
                                  Symbol *ftd = symtable_lookup(cg->sym, fty->base);
                                  if (!(ftd && ftd->kind==SYM_TYPEDEF && ftd->type)) break;
                                  if (ftd->type == fty) break; /* guard against a self-referential typedef */
                                  fty = ftd->type;
                              }
                              if (fty->pointer_depth > 0) load64 = cg->is_64bit ? 1 : 0;
                              /* Fixed-size array field: array decays to pointer to first element.
                               * Return the address of the field instead of loading its value.
                               * IMPORTANT: Use upper-bound guard < 0x40000000 to prevent false positives
                               * when squash zero-extends int field -1 to 4294967295. */
                              if ((fty->array_size > 0 && fty->array_size < 0x40000000) ||
                                  (ff->field.array_size > 0 && ff->field.array_size < 0x40000000))
                                  is_array_field = 1;
                              field_is_unsigned = fty->is_unsigned;
                              field_sz = typeinfo_size(fty, cg->is_64bit);
                              if (field_sz < 1) field_sz = 1;
                              field_found = 1;
                              break;
                          }
                      }
                  }
              }
          }
          if (is_array_field) {
              /* Array field: return address (add offset, do NOT load value) */
              if (foff > 0) asm_add_imm(a, REG_RAX, foff);
          } else if (load64) {
              /* 64-bit pointer load */
              if (foff == 0) {
                  asm_emit3(a,0x48,0x8B,0x00); /* mov rax,[rax] */
              } else if (foff < 128) {
                  asm_emit4(a,0x48,0x8B,0x40,(uint8_t)foff); /* mov rax,[rax+disp8] */
              } else {
                  asm_emit3(a,0x48,0x8B,0x80); asm_emit_u32(a,(uint32_t)foff);
              }
          } else if (cg->is_64bit) {
              /* 64-bit mode: use movsxd for signed 4-byte, mov rax for 8-byte.
               * 1-byte/2-byte fields (e.g. a struct field declared "Uint8"/
               * "Uint16"/"uint8_t"/"uint16_t"/"short") fell through to the
               * final plain 4-byte "mov eax,[eax+foff]" below, unlike the
               * 32-bit-mode branch a few lines down (which already had
               * correct movzx byte/word handling) — this silently read the
               * NEXT field's bytes too (or adjacent struct/array memory for
               * a trailing field), corrupting every narrow struct field
               * read in 64-bit builds. Confirmed via a standalone repro:
               * "struct { Uint16 from; Uint16 to0; } m = {100,200};" read
               * m.from as 0x00C80064 (100 | 200<<16) instead of 100 — this
               * was the real reason SDL3's Unicode case-folding hash-bucket
               * mapping tables (Uint16 from/to0 fields) produced garbage. */
              if (field_found && field_sz == 1) {
                  if (field_is_unsigned) {
                      if (foff == 0) { asm_emit3(a,0x0F,0xB6,0x00); }
                      else if (foff < 128) { asm_emit4(a,0x0F,0xB6,0x40,(uint8_t)foff); }
                      else { asm_emit3(a,0x0F,0xB6,0x80); asm_emit_u32(a,(uint32_t)foff); }
                  } else {
                      if (foff == 0) { asm_emit3(a,0x0F,0xBE,0x00); }
                      else if (foff < 128) { asm_emit4(a,0x0F,0xBE,0x40,(uint8_t)foff); }
                      else { asm_emit3(a,0x0F,0xBE,0x80); asm_emit_u32(a,(uint32_t)foff); }
                  }
              } else if (field_found && field_sz == 2) {
                  if (field_is_unsigned) {
                      if (foff == 0) { asm_emit3(a,0x0F,0xB7,0x00); }
                      else if (foff < 128) { asm_emit4(a,0x0F,0xB7,0x40,(uint8_t)foff); }
                      else { asm_emit3(a,0x0F,0xB7,0x80); asm_emit_u32(a,(uint32_t)foff); }
                  } else {
                      if (foff == 0) { asm_emit3(a,0x0F,0xBF,0x00); }
                      else if (foff < 128) { asm_emit4(a,0x0F,0xBF,0x40,(uint8_t)foff); }
                      else { asm_emit3(a,0x0F,0xBF,0x80); asm_emit_u32(a,(uint32_t)foff); }
                  }
              } else if (field_found && !field_is_unsigned && field_sz == 4) {
                  if (foff == 0) {
                      asm_emit3(a,0x48,0x63,0x00);
                  } else if (foff < 128) {
                      asm_emit4(a,0x48,0x63,0x40,(uint8_t)foff);
                  } else {
                      asm_emit3(a,0x48,0x63,0x80); asm_emit_u32(a,(uint32_t)foff);
                  }
              } else if (field_found && field_sz == 8) {
                  if (foff == 0) asm_emit3(a,0x48,0x8B,0x00);
                  else if (foff < 128) asm_emit4(a,0x48,0x8B,0x40,(uint8_t)foff);
                  else { asm_emit3(a,0x48,0x8B,0x80); asm_emit_u32(a,(uint32_t)foff); }
              } else {
                  if (foff == 0) {
                      asm_emit2(a,0x8B,0x00);
                  } else if (foff < 128) {
                      asm_emit3(a,0x8B,0x40,(uint8_t)foff);
                  } else {
                      asm_emit2(a,0x8B,0x80); asm_emit_u32(a,(uint32_t)foff);
                  }
              }
          } else {
              /* 32-bit mode loads */
              if (field_found && field_sz == 1) {
                  /* movzx eax, byte[eax+foff] */
                  if (foff == 0) { asm_emit3(a,0x0F,0xB6,0x00); }
                  else if (foff < 128) { asm_emit4(a,0x0F,0xB6,0x40,(uint8_t)foff); }
                  else { asm_emit3(a,0x0F,0xB6,0x80); asm_emit_u32(a,(uint32_t)foff); }
              } else if (field_found && field_sz == 2) {
                  /* movzx eax, word[eax+foff] */
                  if (foff == 0) { asm_emit3(a,0x0F,0xB7,0x00); }
                  else if (foff < 128) { asm_emit4(a,0x0F,0xB7,0x40,(uint8_t)foff); }
                  else { asm_emit3(a,0x0F,0xB7,0x80); asm_emit_u32(a,(uint32_t)foff); }
              } else {
                  /* 4-byte load: mov eax, [eax+foff] */
                  if (foff == 0) {
                      asm_emit2(a,0x8B,0x00);
                  } else if (foff < 128) {
                      asm_emit3(a,0x8B,0x40,(uint8_t)foff);
                  } else {
                      asm_emit2(a,0x8B,0x80); asm_emit_u32(a,(uint32_t)foff);
                  }
              }
          }
        }
        break;
    }

    case AST_ASSIGN: {
        const char *op=n->assign.op;
        /* Whole-struct assignment: "*a = *b;", "var = *b;", or "*a = var;"
         * (either side a struct/union pointer dereference, the other side
         * possibly a plain struct-typed variable — e.g. SDL3's own
         * hashtable insert_item() Robin Hood displacement: "temp_item =
         * *candidate;" / "*candidate = *item_to_insert;" / "*item_to_insert
         * = temp_item;", a mix of all three shapes). Must be checked
         * before anything else in this case, and handled as an
         * address-to-address byte copy — squash's normal assignment path
         * below only ever moves a single register's worth of bytes, which
         * silently truncates any multi-field struct copy. See
         * deref_struct_size()'s comment for why this matters. */
        if (strcmp(op,"=")==0) {
            int lsz = struct_copy_size_of(cg, n->assign.lhs);
            int rsz = struct_copy_size_of(cg, n->assign.rhs);
            if (lsz>0 && lsz==rsz) {
                struct_copy_addr_of(cg, n->assign.rhs);          /* RAX = src addr */
                asm_push_reg(a, REG_RAX);
                struct_copy_addr_of(cg, n->assign.lhs);          /* RAX = dst addr */
                asm_mov_reg_reg(a, REG_RBX, REG_RAX);            /* RBX = dst addr */
                asm_pop_reg(a, REG_RAX);                          /* RAX = src addr */
                int off = 0;
                for (; off+8<=lsz; off+=8) {
                    asm_mov_reg_mem(a, REG_RCX, REG_RAX, off);
                    asm_mov_mem_reg(a, REG_RBX, off, REG_RCX);
                }
                for (; off+4<=lsz; off+=4) {
                    asm_mov_reg32_mem(a, REG_RCX, REG_RAX, off);
                    asm_mov_mem32_reg(a, REG_RBX, off, REG_RCX);
                }
                if (off < lsz) {
                    /* asm_movzx_eax_mem8 hardcodes EAX as its destination,
                     * so RAX (still our "src" base register here) can't
                     * also be the addressing base for this load without
                     * clobbering itself between iterations — move src into
                     * RDX first and free up RAX/EAX as pure scratch. */
                    asm_mov_reg_reg(a, REG_RDX, REG_RAX);
                    for (; off<lsz; off++) {
                        asm_movzx_eax_mem8(a, REG_RDX, off);
                        asm_mov_mem8_reg(a, REG_RBX, off, REG_RAX);
                    }
                }
                break;
            }
        }
        /* Float assignment: if RHS is float type, use SSE2/x87 path */
        /* Float assignment: handle "=" and compound "+=","-=","*=","/=" for float vars */
        {
            int rhs_is_float = codegen_is_float_expr(cg, n->assign.rhs);
            int lhs_is_float_var = 0;
            Symbol *fsv = NULL;
            if (n->assign.lhs->kind==AST_VAR) {
                fsv = symtable_lookup(cg->sym, n->assign.lhs->var.name);
                if (fsv && fsv->type) lhs_is_float_var = typeinfo_is_float(fsv->type);
            } else if (n->assign.lhs->kind==AST_MEMBER || n->assign.lhs->kind==AST_INDEX ||
                       n->assign.lhs->kind==AST_DEREF) {
                /* See lvalue_target_is_float()'s comment: without this, a
                 * plain-integer-literal RHS (rhs_is_float==0) into a
                 * float-typed member/array-element/deref target skipped the
                 * float path entirely and stored raw integer bits instead of
                 * a real float conversion. */
                lhs_is_float_var = lvalue_target_is_float(cg, n->assign.lhs);
            }
            if (lhs_is_float_var || (strcmp(op,"=")==0 && rhs_is_float)) {
                int is_compound = (strcmp(op,"=")==0) ? 0 : 1;
                if (is_compound && fsv && cg->is_64bit &&
                    (fsv->kind==SYM_VAR||fsv->kind==SYM_PARAM)) {
                    /* Spill old lhs value onto stack (NOT XMM1, which rhs eval may clobber).
                     * Must load with the variable's ACTUAL stored width: a `float` local is
                     * written via movss (4 bytes) at the "store XMM0 back" step below, but
                     * this reload used to always do an 8-byte movsd — reading 4 bytes past
                     * the real value as the upper half of a "double". Those bytes are stack
                     * padding that happens to read back as zero, so the reinterpreted
                     * "double" was a vanishingly small denormal (float bits in the low
                     * 32 bits, zero above) — negligible next to the new rhs once re-narrowed
                     * to float, so `angle += 0.03f` inside a loop silently behaved like
                     * `angle = 0.03f` every time instead of actually accumulating. */
                    int oldfsz = fsv->type ? typeinfo_size(fsv->type,cg->is_64bit) : 8;
                    if (oldfsz==4) { asm_movss_load(a,0,REG_RBP,fsv->offset); asm_cvtss2sd(a,0,0); }
                    else asm_movsd_load(a,0,REG_RBP,fsv->offset); /* XMM0 = old lhs */
                    asm_sub_rsp(a,16);
                    asm_emit4(a,0xF2,0x0F,0x11,0x04); asm_emit1(a,0x24); /* movsd [rsp],xmm0 */
                } else if (is_compound && !fsv && cg->is_64bit &&
                           (n->assign.lhs->kind==AST_MEMBER ||
                            n->assign.lhs->kind==AST_INDEX  ||
                            n->assign.lhs->kind==AST_DEREF)) {
                    /* Same "spill old lhs onto stack before evaluating
                     * rhs" dance as the SYM_VAR/PARAM/GLOBAL branches
                     * above, for a member/array-element/deref lhs (e.g.
                     * "f->cursor_x += w;" or "arr[i].total += x;") — this
                     * branch didn't exist at all until now, meaning the
                     * SAME missing-push-but-unconditional-pop bug applied
                     * here too (confirmed via a standalone repro: a
                     * struct field accumulated through a pointer in a
                     * loop, "f->cursor_x += w + gap;", produced a
                     * non-monotonic running total instead of a real
                     * accumulation — this is what was silently corrupting
                     * SQW's own word-wrap cursor position before layout.c
                     * was rewritten to avoid the pattern; fixing it here
                     * properly means future code doesn't have to). Unlike
                     * the other lhs kinds, the "address" itself (not just
                     * the old value) must survive across rhs evaluation,
                     * since the eventual store needs it again and
                     * rhs evaluation is free to clobber RAX/RBX — computed
                     * once here and pushed, then reused (popped, not
                     * recomputed via a second codegen_lvalue call) by the
                     * is_compound branch of the store dispatch below. */
                    codegen_lvalue(cg, n->assign.lhs); /* RAX = address of LHS */
                    asm_push_reg(a, REG_RAX);
                    int msz = float_store_target_size(cg, n->assign.lhs);
                    if (msz==4) { asm_movss_load(a,0,REG_RAX,0); asm_cvtss2sd(a,0,0); }
                    else asm_movsd_load(a,0,REG_RAX,0); /* XMM0 = old lhs */
                    asm_sub_rsp(a,16);
                    asm_emit4(a,0xF2,0x0F,0x11,0x04); asm_emit1(a,0x24); /* movsd [rsp],xmm0 */
                } else if (is_compound && fsv && cg->is_64bit && fsv->kind==SYM_GLOBAL) {
                    /* Same "spill old lhs onto stack before evaluating rhs"
                     * dance as the SYM_VAR/SYM_PARAM branch just above, for
                     * a global lhs -- this branch didn't exist at all until
                     * now, meaning "someGlobalFloat += x;" (or -=/*=//=)
                     * unconditionally popped an "old lhs" that was NEVER
                     * pushed a few lines below (that pop/add-rsp is
                     * unconditional on is_compound, not on which of these
                     * two branches ran) — reading 16 bytes of unrelated
                     * stack contents as the old value AND leaving RSP
                     * desynced by 16 bytes for the rest of the function
                     * (confirmed via a standalone repro: a file-scope
                     * "static float total; total += w + gap;" called
                     * repeatedly in a loop produced a wrong, non-
                     * monotonic running total instead of a real
                     * accumulation, e.g. 342, 206, 180, 368 instead of
                     * 342, 503, 638, 961). Global storage is always the
                     * full declared width already (see the plain "="
                     * SYM_GLOBAL branches elsewhere in this function,
                     * which narrow correctly), so no width-detection dance
                     * is needed here — just load and spill it. */
                    const char *oldlbl=(fsv->dll&&fsv->dll[0])?fsv->dll:n->assign.lhs->var.name;
                    int oldgfsz = fsv->type ? typeinfo_size(fsv->type,cg->is_64bit) : 8;
                    asm_lea_rip_wdata(a,REG_RBX,oldlbl);
                    if (oldgfsz==4) { asm_movss_load(a,0,REG_RBX,0); asm_cvtss2sd(a,0,0); }
                    else asm_movsd_load(a,0,REG_RBX,0); /* XMM0 = old lhs */
                    asm_sub_rsp(a,16);
                    asm_emit4(a,0xF2,0x0F,0x11,0x04); asm_emit1(a,0x24); /* movsd [rsp],xmm0 */
                }
                /* Evaluate rhs into XMM0 */
                if (rhs_is_float) codegen_float_expr(cg, n->assign.rhs);
                else { codegen_expr(cg, n->assign.rhs); asm_cvtsi2sd(a, 0, REG_RAX); }
                /* Apply compound op: pop old lhs -> XMM1, then XMM1 op XMM0 -> XMM0 */
                if (is_compound && cg->is_64bit) {
                    asm_movsd_xmm(a,1,0);                    /* XMM1 = rhs */
                    asm_emit4(a,0xF2,0x0F,0x10,0x04); asm_emit1(a,0x24); /* movsd xmm0,[rsp]=old lhs */
                    asm_add_rsp(a,16);
                    if      (strcmp(op,"+=")==0) asm_addsd(a,0,1); /* XMM0 = lhs + rhs */
                    else if (strcmp(op,"-=")==0) asm_subsd(a,0,1); /* XMM0 = lhs - rhs */
                    else if (strcmp(op,"*=")==0) asm_mulsd(a,0,1); /* XMM0 = lhs * rhs */
                    else if (strcmp(op,"/=")==0) asm_divsd(a,0,1); /* XMM0 = lhs / rhs */
                } else if (is_compound && !cg->is_64bit) {
                    /* 32-bit: load lhs on x87 stack, apply op */
                    if (fsv && (fsv->kind==SYM_VAR||fsv->kind==SYM_PARAM)) {
                        /* ST0=rhs already loaded; push lhs */
                        /* For 32-bit compound float: fall through to integer path for now */
                        /* TODO: implement properly */
                    }
                }
                /* Store XMM0 back */
                if (fsv && (fsv->kind==SYM_VAR||fsv->kind==SYM_PARAM)) {
                    int fsz=fsv->type?typeinfo_size(fsv->type,cg->is_64bit):8;
                    if (cg->is_64bit) {
                        if (fsz==4){asm_cvtsd2ss(a,0,0);asm_movss_store(a,REG_RBP,fsv->offset,0);}
                        else asm_movsd_store(a,REG_RBP,fsv->offset,0);
                    } else { asm_fstp_mem64(a,REG_EBP,fsv->offset); asm_fld_mem64(a,REG_EBP,fsv->offset); }
                } else if (fsv && fsv->kind==SYM_GLOBAL) {
                    const char *lbl=(fsv->dll&&fsv->dll[0])?fsv->dll:n->assign.lhs->var.name;
                    if (cg->is_64bit){
                        /* Match the SYM_VAR/SYM_PARAM branch just above:
                         * narrow to single precision before storing when
                         * the global itself is declared "float" (4 bytes),
                         * not "double" (8) -- an unconditional 8-byte
                         * movsd here silently corrupted a `float` global's
                         * own storage (only 4 bytes actually allocated for
                         * it) and left it unreadable (a later float-sized
                         * read via movss saw only the double bit pattern's
                         * low 4 bytes, exactly zero for any "round" value —
                         * confirmed via a standalone repro: "float g; void
                         * f(float h){g=h;}" left g reading back as 0.0 for
                         * every non-fractional h). */
                        int gfsz=fsv->type?typeinfo_size(fsv->type,cg->is_64bit):8;
                        asm_lea_rip_wdata(a,REG_RBX,lbl);
                        if (gfsz==4){asm_cvtsd2ss(a,0,0);asm_movss_store(a,REG_RBX,0,0);}
                        else asm_movsd_store(a,REG_RBX,0,0);
                    }
                    else {
                        /* 32-bit: fstp to global wdata address, then reload */
                        asm_emit2(a,0xDD,0x1D); asm_reloc_wdata(a,lbl); /* fstp qword [lbl] */
                        asm_emit2(a,0xDD,0x05); asm_reloc_wdata(a,lbl); /* fld  qword [lbl] */
                    }
                } else if (!fsv && cg->is_64bit &&
                           (n->assign.lhs->kind==AST_MEMBER ||
                            n->assign.lhs->kind==AST_INDEX  ||
                            n->assign.lhs->kind==AST_DEREF)) {
                    /* LHS is a member/array/deref: get its address into
                     * RAX. Plain "=": compute it fresh here (rhs is
                     * already safely in XMM0, and codegen_lvalue only
                     * touches integer registers). Compound "+="/etc: the
                     * pre-rhs branch above already computed this same
                     * address and pushed it (it had to survive across
                     * rhs evaluation, which recomputing here can't
                     * retroactively fix) — reuse that instead of
                     * recomputing, both for correctness when the lvalue
                     * expression has side effects and to keep the push/pop
                     * this function does balanced. Store width must match
                     * the target's actual size — always storing 8 bytes
                     * corrupts whatever 4-byte `float` element/field
                     * follows it in memory. */
                    if (is_compound) asm_pop_reg(a, REG_RAX);
                    else codegen_lvalue(cg, n->assign.lhs); /* RAX = address of LHS */
                    if (float_store_target_size(cg, n->assign.lhs) == 4) {
                        asm_cvtsd2ss(a,0,0);
                        asm_emit4(a,0xF3,0x0F,0x11,0x00); /* movss [rax], xmm0 */
                    } else {
                        asm_emit4(a,0xF2,0x0F,0x11,0x00); /* movsd [rax], xmm0 */
                    }
                } else if (!fsv && !cg->is_64bit &&
                           (n->assign.lhs->kind==AST_MEMBER ||
                            n->assign.lhs->kind==AST_INDEX  ||
                            n->assign.lhs->kind==AST_DEREF)) {
                    /* 32-bit: ST0 holds float value; store to member/deref address.
                     * Integer lvalue ops (LEA/ADD) preserve x87 ST0. */
                    codegen_lvalue(cg, n->assign.lhs); /* EAX = address of LHS */
                    asm_fstp_mem64(a, REG_EAX, 0);     /* fstp qword ptr [eax] */
                    asm_fld_mem64(a, REG_EAX, 0);      /* fld  qword ptr [eax] — reload */
                }
                if (cg->is_64bit){asm_emit4(a,0x66,0x48,0x0F,0x7E);asm_emit1(a,0xC0);} /* movq rax,xmm0 */
                break;
            }
        }
        codegen_expr(cg,n->assign.rhs);
        /* Compound assignment: combine with lhs */
        if (strcmp(op,"=")!=0) {
            asm_push_reg(a,REG_RAX);
            codegen_expr(cg,n->assign.lhs);
            asm_pop_reg(a,REG_RBX);
            if      (strcmp(op,"+=")==0)  asm_add_reg_reg(a,REG_RAX,REG_RBX);
            else if (strcmp(op,"-=")==0)  asm_sub_reg_reg(a,REG_RAX,REG_RBX);
            else if (strcmp(op,"*=")==0)  asm_imul_reg_reg(a,REG_RAX,REG_RBX);
            else if (strcmp(op,"/=")==0)  { asm_mov_reg_reg(a,REG_RCX,REG_RBX); if (expr_is_unsigned_int(cg,n->assign.lhs)) asm_div_reg(a,REG_RCX); else asm_idiv_reg(a,REG_RCX); }
            else if (strcmp(op,"%=")==0)  { asm_mov_reg_reg(a,REG_RCX,REG_RBX); if (expr_is_unsigned_int(cg,n->assign.lhs)) asm_div_reg(a,REG_RCX); else asm_idiv_reg(a,REG_RCX); asm_mov_reg_reg(a,REG_RAX,REG_RDX); }
            else if (strcmp(op,"&=")==0)  asm_and_reg_reg(a,REG_RAX,REG_RBX);
            else if (strcmp(op,"|=")==0)  asm_or_reg_reg (a,REG_RAX,REG_RBX);
            else if (strcmp(op,"^=")==0)  asm_xor_reg_reg(a,REG_RAX,REG_RBX);
            else if (strcmp(op,"<<=")==0) { asm_mov_reg_reg(a,REG_RCX,REG_RBX); asm_shl_reg_cl(a,REG_RAX); }
            else if (strcmp(op,">>=")==0) { asm_mov_reg_reg(a,REG_RCX,REG_RBX); asm_shr_reg_cl(a,REG_RAX); }
        }
        /* store result back */
        if (n->assign.lhs->kind==AST_VAR) {
            Symbol *s=symtable_lookup(cg->sym,n->assign.lhs->var.name);
            if (s && (s->kind==SYM_VAR||s->kind==SYM_PARAM)) {
                if (cg->is_64bit && s->type && s->type->pointer_depth == 0) {
                    int vsz = sizeof_type_sym(s->type, 1, cg->sym);
                    if      (vsz == 1) asm_mov_mem8_reg  (a,REG_RBP,s->offset,REG_RAX);
                    else if (vsz <= 4) asm_mov_mem32_reg (a,REG_RBP,s->offset,REG_RAX);
                    else               asm_mov_mem_reg   (a,REG_RBP,s->offset,REG_RAX);
                } else {
                    asm_mov_mem_reg(a,REG_RBP,s->offset,REG_RAX);
                }
            } else if (s && s->kind==SYM_GLOBAL) {
                const char *glbl = (s->dll && s->dll[0]) ? s->dll : n->assign.lhs->var.name;
                asm_push_reg(a,REG_RAX);
                if (cg->is_64bit) {
                    asm_lea_rip_wdata(a,REG_RBX,glbl); /* all globals in wdata */
                } else {
                    asm_emit1(a,0xBB); asm_reloc_wdata(a,glbl);
                }
                asm_pop_reg(a,REG_RAX);
                if (cg->is_64bit && s->type && (s->type->pointer_depth > 0 || sizeof_type_sym(s->type, 1, cg->sym) == 8))
                    asm_emit3(a,0x48,0x89,0x03); /* REX.W mov [rbx], rax */
                else
                    asm_emit2(a,0x89,0x03);  /* mov [rbx/ebx], eax */
            } else if (!s) {
                /* Assigning to a genuinely undefined identifier (e.g.
                 * "undeclared_counter = 5;") previously silently no-opped
                 * here — the RHS was still evaluated (for any side effects),
                 * but the store-back was just skipped with no diagnostic at
                 * all, unlike every other undefined-identifier path in this
                 * file. Give it the same treatment as a plain read. */
                char sugbuf[160] = "";
                const char *suggestion = diag_suggest_name(cg->sym, n->assign.lhs->var.name);
                if (suggestion) snprintf(sugbuf, sizeof sugbuf, " (did you mean '%s'?)", suggestion);
                diag_emit(DIAG_ERROR, n->line, cg->cur_func_name, n->assign.lhs->var.name,
                          "undefined identifier '%s' used as an assignment target%s",
                          n->assign.lhs->var.name, sugbuf);
            }
        } else {
            asm_push_reg(a,REG_RAX);
            codegen_lvalue(cg,n->assign.lhs);
            asm_pop_reg(a,REG_RBX);
            /* Determine store width from LHS element size.
             * Array subscripts of pointer type need 64-bit store in 64-bit mode.
             * Struct member fields and plain int arrays use 32-bit store.         */
            /* Determine store width from LHS type, not RHS guesswork.
             * For array subscripts use elem_size_of; for struct members
             * look up the actual field TypeInfo; for pointer vars use 8. */
            int store_sz = 4;
            {
                ASTNode *lhs = n->assign.lhs;
                if (lhs->kind == AST_INDEX) {
                    int esz = elem_size_of(cg, lhs->index.array);
                    if (!cg->is_64bit) { if (esz == 1 || esz == 2) store_sz = esz; }
                    else store_sz = esz;
                } else if (cg->is_64bit && lhs->kind == AST_MEMBER) {
                    /* Walk to the containing struct and get the field's TypeInfo */
                    ASTNode *obj = lhs->member.obj;
                    const char *fname = lhs->member.field;
                    const char *stype = NULL;
                    if (obj && obj->kind == AST_VAR) {
                        Symbol *sv = symtable_lookup(cg->sym, obj->var.name);
                        if (sv && sv->type) stype = sv->type->base;
                    } else if (obj && obj->kind == AST_DEREF) {
                        /* ptr->field: find type of pointer operand */
                        ASTNode *op = obj->deref.operand;
                        if (op && op->kind == AST_VAR) {
                            Symbol *sv = symtable_lookup(cg->sym, op->var.name);
                            if (sv && sv->type && sv->type->pointer_depth > 0) {
                                /* strip one pointer level to get struct type */
                                stype = sv->type->base;
                            }
                        }
                    } else if (obj && (obj->kind == AST_MEMBER || obj->kind == AST_INDEX)) {
                        /* Chained member: n->struct_.field = val — use recursive resolver */
                        stype = resolve_node_type(cg->sym, obj);
                    }
                    if (stype) {
                        const char *bare = stype;
                        if (strncmp(bare,"struct ",7)==0) bare+=7;
                        else if (strncmp(bare,"union ",6)==0) bare+=6;
                        char sk[256]; snprintf(sk,sizeof sk,"struct %s",bare);
                        Symbol *ss = symtable_lookup(cg->sym, sk);
                        if (!ss) {
                            /* try typedef resolution */
                            Symbol *td = symtable_lookup(cg->sym, stype);
                            if (td && td->kind==SYM_TYPEDEF && td->type) {
                                bare = td->type->base;
                                if (strncmp(bare,"struct ",7)==0) bare+=7;
                                else if (strncmp(bare,"union ",6)==0) bare+=6;
                                snprintf(sk,sizeof sk,"struct %s",bare);
                                ss = symtable_lookup(cg->sym, sk);
                            }
                        }
                        if (ss && ss->struct_node) {
                            for (int _i=0; _i<ss->struct_node->struct_decl.nfields; _i++) {
                                ASTNode *ff = ss->struct_node->struct_decl.fields[_i];
                                if (ff && ff->kind==AST_FIELD && ff->field.name &&
                                    strcmp(ff->field.name, fname)==0 && ff->field.type) {
                                    /* Resolve typedef'd field types before checking
                                     * pointer_depth — see matching comment (and the
                                     * "must LOOP, not just resolve one level" typedef-
                                     * chain explanation) on the read side (codegen_expr's
                                     * AST_MEMBER case) for why a single-hop `if` isn't
                                     * enough. sizeof_type_sym() below already recurses
                                     * through the full typedef chain on its own, so
                                     * store_sz itself was already correct even before this
                                     * loop; this only keeps `fty->pointer_depth` (used
                                     * below) consistent with what was actually resolved. */
                                    TypeInfo *fty = ff->field.type;
                                    for (int _td_hops = 0; _td_hops < 8 && fty->pointer_depth==0 && fty->base; _td_hops++) {
                                        Symbol *ftd = symtable_lookup(cg->sym, fty->base);
                                        if (!(ftd && ftd->kind==SYM_TYPEDEF && ftd->type)) break;
                                        if (ftd->type == fty) break;
                                        fty = ftd->type;
                                    }
                                    store_sz = sizeof_type_sym(fty, 1, cg->sym);
                                    if (fty->pointer_depth > 0) store_sz = 8;
                                    if (getenv("SQUASH_FBO_DEBUG")) {
                                        fprintf(stderr, "[ASSIGN-store] field='%s' fty->base='%s' store_sz=%d\n",
                                                fname, fty->base?fty->base:"(null)", store_sz);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                } else if (cg->is_64bit && lhs->kind == AST_VAR) {
                    Symbol *sv = symtable_lookup(cg->sym, lhs->var.name);
                    if (sv && sv->type) {
                        if (sv->type->pointer_depth > 0) store_sz = 8;
                        else if (typeinfo_size(sv->type, 1) == 8) store_sz = 8;
                    }
                } else if (lhs->kind == AST_DEREF) {
                    /* *ptr = val — width from ptr's base type. Unwrap postfix
                     * ++ (e.g. "*dstp1++ = *srcp1++;") to match the AST_DEREF
                     * read side's own unwrapping — otherwise the store side
                     * never sees past the AST_UNARY wrapper at all. */
                    ASTNode *op = lhs->deref.operand;
                    if (op && op->kind == AST_CAST && op->cast.type) {
                        /* *(T*)expr = val — an explicit cast right before the
                         * deref (e.g. dlmalloc-style free-list linking:
                         * "*(void **)block = pool->free_blocks;") names the
                         * pointee type directly, so use it instead of trying
                         * to trace back through the cast to some declared
                         * variable's type (which the unwrap loop below can't
                         * do — it bails to NULL on hitting an AST_CAST,
                         * silently leaving store_sz at its 4-byte default).
                         * Confirmed via SDL3's real SDL_audioqueue.c: this
                         * truncated the free-list "next" pointer write to 32
                         * bits, leaving the upper half as leftover heap
                         * garbage that later got dereferenced as a pointer
                         * and crashed. */
                        TypeInfo *ct = op->cast.type;
                        if (ct->pointer_depth > 1) store_sz = cg->is_64bit ? 8 : 4;
                        else if (ct->pointer_depth == 1) {
                            const char *b2 = ct->base ? ct->base : "";
                            if (strncmp(b2,"unsigned ",9)==0) b2+=9;
                            if (strcmp(b2,"char")==0||strcmp(b2,"int8_t")==0||strcmp(b2,"uint8_t")==0)
                                store_sz = 1;
                            else if (strcmp(b2,"short")==0||strcmp(b2,"int16_t")==0||strcmp(b2,"uint16_t")==0)
                                store_sz = 2;
                            else if (is_byte_sized_stdint(cg->sym, ct->base))
                                store_sz = 1;
                            else if (is_short_sized_stdint(cg->sym, ct->base))
                                store_sz = 2;
                            else {
                                Symbol *btd = symtable_lookup(cg->sym, ct->base ? ct->base : "");
                                TypeInfo *bt = (btd && btd->kind==SYM_TYPEDEF) ? btd->type : NULL;
                                if (bt && bt->pointer_depth > 0) store_sz = cg->is_64bit ? 8 : 4;
                                else if (bt && bt->base && sizeof_type_sym(bt, cg->is_64bit, cg->sym) == 8) store_sz = 8;
                                else if (ct->base && sizeof_type_sym(ct, cg->is_64bit, cg->sym) == 8) store_sz = 8;
                            }
                        }
                        goto assign_store_sz_done;
                    }
                    while (op && op->kind != AST_VAR) {
                        if (op->kind==AST_UNARY && op->unary.operand) op = op->unary.operand;
                        else if (op->kind==AST_BINARY && op->binary.left) op = op->binary.left;
                        else if (op->kind==AST_ASSIGN && op->assign.lhs) op = op->assign.lhs;
                        else { op = NULL; break; }
                    }
                    if (op && op->kind == AST_VAR) {
                        Symbol *sv = symtable_lookup(cg->sym, op->var.name);
                        if (sv && sv->type && sv->type->pointer_depth > 1) store_sz = cg->is_64bit ? 8 : 4;
                        else if (sv && sv->type && sv->type->pointer_depth==1) {
                            /* *charptr = val — 1 byte */
                            const char *b2=sv->type->base;
                            if (strncmp(b2,"unsigned ",9)==0) b2+=9;
                            if (strcmp(b2,"char")==0||strcmp(b2,"int8_t")==0||strcmp(b2,"uint8_t")==0)
                                store_sz=1;
                            else if (strcmp(b2,"short")==0||strcmp(b2,"int16_t")==0||strcmp(b2,"uint16_t")==0)
                                store_sz=2;
                            else if (is_byte_sized_stdint(cg->sym, sv->type->base))
                                store_sz=1;
                            else if (is_short_sized_stdint(cg->sym, sv->type->base))
                                store_sz=2;
                            else {
                                /* None of the byte/short cases matched — this
                                 * silently fell through to the 4-byte default
                                 * above for ANY other pointee type, including
                                 * a typedef'd (function) pointer, e.g.
                                 * "typedef void*(*ReallocFn)(void*,size_t);
                                 * ReallocFn *out; *out = some_func;" — the
                                 * store only wrote the low 32 bits of the
                                 * 8-byte function address, truncating the
                                 * upper half to whatever garbage was already
                                 * in [rax+4..7]. Confirmed via a minimal
                                 * repro: "*out = real_realloc;" through a
                                 * "ReallocFn *out" parameter read back with
                                 * its top 32 bits zeroed. Resolve one typedef
                                 * hop (matching the pattern used throughout
                                 * this file) and check whether the pointee
                                 * type itself is pointer-shaped (or a known
                                 * 8-byte scalar) before falling back to 4. */
                                Symbol *btd = symtable_lookup(cg->sym, sv->type->base);
                                TypeInfo *bt = (btd && btd->kind==SYM_TYPEDEF) ? btd->type : NULL;
                                if (bt && bt->pointer_depth > 0) {
                                    store_sz = cg->is_64bit ? 8 : 4;
                                } else if (bt && bt->base && sizeof_type_sym(bt, cg->is_64bit, cg->sym) == 8) {
                                    store_sz = 8;
                                }
                            }
                        }
                    }
                }
            }
            assign_store_sz_done: ;
            if (store_sz == 8 && cg->is_64bit) {
                /* mov [rax],rbx — 64-bit store for pointer-sized elements */
                asm_emit3(a,0x48,0x89,0x18);
            } else if (store_sz == 1) {
                asm_emit2(a,0x88,0x18); /* mov byte ptr [rax],bl */
            } else if (store_sz == 2) {
                asm_emit3(a,0x66,0x89,0x18); /* mov word ptr [rax],bx */
            } else if (!cg->is_64bit) {
                asm_emit2(a,0x89,0x18); /* mov [eax],ebx */
            } else {
                asm_emit2(a,0x89,0x18); /* mov [rax],ebx — 32-bit store */
            }
            asm_mov_reg_reg(a,REG_RAX,REG_RBX);
        }
        break;
    }

    case AST_UNARY: {
        const char *op=n->unary.op;
        /* Store width for "++"/"--" applied to a struct-member lvalue
         * (details->Rshift++ / ++details->Rshift). The write-back below used
         * to hardcode a 4-byte "mov [rax],ebx" unconditionally regardless of
         * the field's real size — harmless for a genuinely 4-byte field, but
         * for a narrow (1/2-byte) field like SDL3's own Uint8 struct members
         * this clobbers whichever 3 (or 2) bytes happen to follow it in
         * memory. This is exactly what made SDL_InitPixelFormatDetails()
         * (real, unmodified SDL3 source) silently corrupt its own
         * Rshift/Gshift/Bshift/Ashift fields — each is a Uint8 immediately
         * followed by the next, so every "++details->Xshift" zeroed its
         * neighbor's byte, and a later "details->Xbits = 0;"-style store to
         * an EARLIER narrow field (same underlying bug, in AST_ASSIGN, fixed
         * separately above) could zero a LATER field's byte too, once its
         * own 4-byte write extended past its own 1-byte slot. Reuses
         * field_type_of()+sizeof_type_sym() (the same multi-hop
         * typedef-resolving pair already relied on for correct struct
         * layout/offsets) rather than the old one-hop-only typeinfo_size()
         * check, so a typedef CHAIN ending in a narrow type (Uint8 ->
         * uint8_t -> unsigned char, not just a direct one-hop alias) is
         * still recognized. */
        /* "++"/"--" on a FLOAT/DOUBLE-typed plain variable (e.g. real
         * SDL3's BesselI0(): "float i = 1.0f; ... ++i;" — the Kaiser-
         * window resampler-filter series index). The rest of this whole
         * AST_UNARY "++"/"--" block (below) is integer-only: it loads the
         * operand via codegen_expr() into RAX, does an integer add/sub,
         * and stores the result back — for a float operand this loads its
         * raw bit pattern as if it were an integer, adds 1 to those BITS
         * (not to the value), and every caller that observed the "result"
         * afterward saw an unchanged value on read-back once it was
         * store/loaded back with the (also integer) width logic below,
         * silently no-opping the increment entirely. Confirmed via a
         * minimal repro ("float i=1.0f; ++i;") — i stayed exactly 1.0f
         * across repeated ++. This left BesselI0()'s "while (t >= sum *
         * SDL_FLT_EPSILON) { ...; ++i; }" loop's `i` permanently stuck at
         * 1.0, so `x / (i*i)` never shrank and `t` diverged to +infinity
         * instead of converging — a genuine infinite loop once the
         * float-comparison branch-condition bug (see codegen_branch's own
         * fix) stopped masking it by evaluating the loop condition
         * essentially at random. Handled here as a self-contained early
         * case (restricted to a plain local/global scalar variable, the
         * shape this real bug needs) rather than threading float-awareness
         * through the whole existing integer post/prefix implementation
         * below. */
        if ((strcmp(op,"++")==0 || strcmp(op,"--")==0) && n->unary.operand &&
            n->unary.operand->kind == AST_VAR && codegen_is_float_expr(cg, n->unary.operand)) {
            Symbol *fs = symtable_lookup(cg->sym, n->unary.operand->var.name);
            int f_is_single = !(fs && fs->type && fs->type->base && strcmp(fs->type->base,"double")==0);
            int f_is_global = fs && fs->kind==SYM_GLOBAL;
            codegen_float_expr(cg, n->unary.operand); /* XMM0 = current value (as double) */
            if (n->unary.post) {
                asm_sub_rsp(a,16);
                asm_emit4(a,0xF2,0x0F,0x11,0x04); asm_emit1(a,0x24); /* movsd [rsp],xmm0 (save original) */
            }
            {
                double one = 1.0;
                const char *one_lbl = intern_float_const(cg, (const char *)&one);
                asm_movsd_rip(a, 1, one_lbl); /* xmm1 = 1.0 */
            }
            if (strcmp(op,"++")==0) asm_addsd(a,0,1); else asm_subsd(a,0,1);
            if (f_is_single) asm_cvtsd2ss(a,0,0);
            if (fs && !f_is_global) {
                if (f_is_single) asm_movss_store(a,REG_RBP,fs->offset,0);
                else             asm_movsd_store(a,REG_RBP,fs->offset,0);
            } else if (f_is_global) {
                const char *glbl=(fs->dll&&fs->dll[0])?fs->dll:n->unary.operand->var.name;
                asm_lea_rip_wdata(a,REG_RBX,glbl);
                if (f_is_single) asm_movss_store(a,REG_RBX,0,0);
                else             asm_movsd_store(a,REG_RBX,0,0);
            }
            if (n->unary.post) {
                asm_emit4(a,0xF2,0x0F,0x10,0x04); asm_emit1(a,0x24); /* movsd xmm0,[rsp] = original */
                asm_add_rsp(a,16);
            }
            /* Leave the result as raw bits in RAX — matches what every
             * other codegen_expr() caller of this switch expects (see
             * codegen_expr's own top-of-function float-expr dispatch,
             * which does the identical movq after calling
             * codegen_float_expr() for AST_FLOAT/AST_BINARY/AST_CAST, but
             * has no AST_UNARY "++"/"--" case of its own to route through
             * — this branch IS that missing case, reached directly here
             * instead). */
            if (cg->is_64bit) { asm_emit4(a,0x66,0x48,0x0F,0x7E); asm_emit1(a,0xC0); }
            break;
        }
        int incdec_store_sz = 4;
        if ((strcmp(op,"++")==0 || strcmp(op,"--")==0) && n->unary.operand &&
            n->unary.operand->kind == AST_MEMBER) {
            ASTNode *mo = n->unary.operand;
            TypeInfo *fty = field_type_of(cg, mo->member.obj, mo->member.field);
            if (fty) {
                if (fty->pointer_depth > 0) incdec_store_sz = cg->is_64bit ? 8 : 4;
                else incdec_store_sz = sizeof_type_sym(fty, cg->is_64bit, cg->sym);
            }
        }
        if (n->unary.post) {
            /* post ++ / -- */
            codegen_expr(cg,n->unary.operand);
            asm_push_reg(a,REG_RAX); /* save original */
            {
                /* Pointer arithmetic scaling: "ptr++"/"ptr--" must advance
                 * by sizeof(*ptr), not by 1 — the same gap already fixed
                 * for the binary "ptr + i" spelling (see the AST_BINARY
                 * comment below) applies equally here, and was the actual
                 * root cause of SDL3's SDL_hashtable.c destroy_all()
                 * silently reading garbage: "for (SDL_HashItem *i = ...;
                 * i < end; ++i)" advanced the loop pointer by only 1 byte
                 * per iteration instead of sizeof(SDL_HashItem). */
                int pscale = pointer_pointee_size(cg, n->unary.operand);
                if (pscale <= 0) pscale = 1;
                asm_mov_reg_imm(a,REG_RBX,pscale);
                if (strcmp(op,"++")==0) asm_add_reg_reg(a,REG_RAX,REG_RBX);
                else asm_sub_reg_reg(a,REG_RAX,REG_RBX);
            }
            if (n->unary.operand->kind==AST_VAR) {
                Symbol *s=symtable_lookup(cg->sym,n->unary.operand->var.name);
                if (s && (s->kind==SYM_VAR||s->kind==SYM_PARAM)) {
                    if (cg->is_64bit && s->type && s->type->pointer_depth == 0) {
                        int vsz = typeinfo_size(s->type, 1);
                        if      (vsz == 1) asm_mov_mem8_reg  (a,REG_RBP,s->offset,REG_RAX);
                        else if (vsz <= 4) asm_mov_mem32_reg (a,REG_RBP,s->offset,REG_RAX);
                        else               asm_mov_mem_reg   (a,REG_RBP,s->offset,REG_RAX);
                    } else {
                        asm_mov_mem_reg(a,REG_RBP,s->offset,REG_RAX);
                    }
                } else if (s && s->kind==SYM_GLOBAL) {
                    const char *glbl=(s->dll&&s->dll[0])?s->dll:n->unary.operand->var.name;
                    asm_push_reg(a,REG_RAX);
                    if (cg->is_64bit) asm_lea_rip_wdata(a,REG_RBX,glbl);
                    else { asm_emit1(a,0xBB); asm_reloc_wdata(a,glbl); }
                    asm_pop_reg(a,REG_RAX);
                    if (cg->is_64bit && s->type && (s->type->pointer_depth > 0 || typeinfo_size(s->type, 1) == 8))
                        asm_emit3(a,0x48,0x89,0x03); /* REX.W mov [rbx], rax */
                    else
                        asm_emit2(a,0x89,0x03); /* mov [rbx/ebx],eax */
                }
            } else if (n->unary.operand->kind==AST_MEMBER ||
                       n->unary.operand->kind==AST_INDEX  ||
                       n->unary.operand->kind==AST_DEREF) {
                /* Store incremented value back to struct/array/deref lvalue */
                asm_push_reg(a,REG_RAX); /* save incremented value */
                codegen_lvalue(cg,n->unary.operand);
                asm_pop_reg(a,REG_RBX); /* incremented value */
                if (incdec_store_sz == 8 && cg->is_64bit) asm_emit3(a,0x48,0x89,0x18); /* mov [rax],rbx */
                else if (incdec_store_sz == 1) asm_emit2(a,0x88,0x18); /* mov byte [rax],bl */
                else if (incdec_store_sz == 2) asm_emit3(a,0x66,0x89,0x18); /* mov word [rax],bx */
                else asm_emit2(a,0x89,0x18); /* mov [rax],ebx (32-bit store) */
            }
            asm_pop_reg(a,REG_RAX); /* return original value */
        } else if (strcmp(op,"++")==0||strcmp(op,"--")==0) {
            codegen_expr(cg,n->unary.operand);
            {
                /* Pointer arithmetic scaling — see the matching comment on
                 * the postfix ++/-- branch above. */
                int pscale = pointer_pointee_size(cg, n->unary.operand);
                if (pscale <= 0) pscale = 1;
                asm_mov_reg_imm(a,REG_RBX,pscale);
                if (strcmp(op,"++")==0) asm_add_reg_reg(a,REG_RAX,REG_RBX);
                else asm_sub_reg_reg(a,REG_RAX,REG_RBX);
            }
            if (n->unary.operand->kind==AST_VAR) {
                Symbol *s=symtable_lookup(cg->sym,n->unary.operand->var.name);
                if (s && (s->kind==SYM_VAR||s->kind==SYM_PARAM)) {
                    if (cg->is_64bit && s->type && s->type->pointer_depth == 0) {
                        int vsz = typeinfo_size(s->type, 1);
                        if      (vsz == 1) asm_mov_mem8_reg  (a,REG_RBP,s->offset,REG_RAX);
                        else if (vsz <= 4) asm_mov_mem32_reg (a,REG_RBP,s->offset,REG_RAX);
                        else               asm_mov_mem_reg   (a,REG_RBP,s->offset,REG_RAX);
                    } else {
                        asm_mov_mem_reg(a,REG_RBP,s->offset,REG_RAX);
                    }
                } else if (s && s->kind==SYM_GLOBAL) {
                    const char *glbl=(s->dll&&s->dll[0])?s->dll:n->unary.operand->var.name;
                    asm_push_reg(a,REG_RAX);
                    if (cg->is_64bit) asm_lea_rip_wdata(a,REG_RBX,glbl);
                    else { asm_emit1(a,0xBB); asm_reloc_wdata(a,glbl); }
                    asm_pop_reg(a,REG_RAX);
                    if (cg->is_64bit && s->type && (s->type->pointer_depth > 0 || typeinfo_size(s->type, 1) == 8))
                        asm_emit3(a,0x48,0x89,0x03); /* REX.W mov [rbx], rax */
                    else
                        asm_emit2(a,0x89,0x03); /* mov [rbx/ebx],eax */
                }
            } else if (n->unary.operand->kind==AST_MEMBER ||
                       n->unary.operand->kind==AST_INDEX  ||
                       n->unary.operand->kind==AST_DEREF) {
                /* Store incremented value back to struct/array/deref lvalue */
                asm_push_reg(a,REG_RAX); /* save incremented value */
                codegen_lvalue(cg,n->unary.operand);
                asm_pop_reg(a,REG_RBX); /* incremented value */
                if (incdec_store_sz == 8 && cg->is_64bit) asm_emit3(a,0x48,0x89,0x18); /* mov [rax],rbx */
                else if (incdec_store_sz == 1) asm_emit2(a,0x88,0x18); /* mov byte [rax],bl */
                else if (incdec_store_sz == 2) asm_emit3(a,0x66,0x89,0x18); /* mov word [rax],bx */
                else asm_emit2(a,0x89,0x18); /* mov [rax],ebx */
                asm_mov_reg_reg(a,REG_RAX,REG_RBX); /* return incremented value */
            }
        } else {
            codegen_expr(cg,n->unary.operand);
            if (strcmp(op,"-")==0) {
                /* Constant folding: -NUMBER becomes a direct negative immediate */
                if (n->unary.operand && n->unary.operand->kind==AST_NUMBER) {
                    asm_mov_reg_imm(a,REG_RAX,-(long long)n->unary.operand->num.value);
                } else {
                    asm_neg_reg(a,REG_RAX);
                }
            }
            else if (strcmp(op,"~")==0) asm_not_reg(a,REG_RAX);
            else if (strcmp(op,"!")==0) {
                asm_test_reg_reg(a,REG_RAX,REG_RAX);
                asm_setcc_al(a,CC_E); asm_movzx_rax_al(a);
            }
        }
        break;
    }

    case AST_BINARY: {
        const char *op=n->binary.op;
        /* short-circuit */
        if (strcmp(op,"&&")==0) {
            int end=asm_new_label(a,"sc_and");
            codegen_expr(cg,n->binary.left);
            asm_test_reg_reg(a,REG_RAX,REG_RAX);
            asm_jcc_label(a,CC_E,end);
            codegen_expr(cg,n->binary.right);
            asm_test_reg_reg(a,REG_RAX,REG_RAX);
            asm_setcc_al(a,CC_NE); asm_movzx_rax_al(a);
            asm_def_label(a,end); break;
        }
        if (strcmp(op,"||")==0) {
            int end=asm_new_label(a,"sc_or");
            codegen_expr(cg,n->binary.left);
            asm_test_reg_reg(a,REG_RAX,REG_RAX);
            asm_jcc_label(a,CC_NE,end);
            codegen_expr(cg,n->binary.right);
            asm_test_reg_reg(a,REG_RAX,REG_RAX);
            asm_setcc_al(a,CC_NE); asm_movzx_rax_al(a);
            asm_def_label(a,end); break;
        }
        /* comma: eval both, result is right */
        if (strcmp(op,",")==0) {
            codegen_expr(cg,n->binary.left);
            codegen_expr(cg,n->binary.right);
            break;
        }
        /* Float-operand comparisons materialized as a VALUE (not directly
         * an if/while condition -- that case is handled separately, and
         * correctly, by codegen_branch's own near-identical fix) fell
         * through to the plain-integer path below: codegen_expr() on each
         * operand followed by an integer CMP, which reads the raw register
         * bits and compares them as if they were integers instead of using
         * UCOMISD. Confirmed via a minimal repro matching this project's
         * own point_in_rect(): "return py < r->y + r->h;" (used inside a
         * boolean chain returned from a function, and equally inside a
         * plain "int x = a < b;") came out true for py=650, r->y+r->h=403
         * -- the exact bug behind SQW's own scrollbar-track clicks and
         * hit-testing intermittently landing in the wrong region. Note this
         * is genuinely a SEPARATE code path from codegen_branch's own fix
         * (a stale comment nearby claimed value-comparisons were "already"
         * covered here -- they were not: codegen_branch compiles straight
         * to CMP+Jcc and never reaches this switch at all, so its fix never
         * touched this one). Mirrors codegen_branch's own UCOMISD sequence
         * exactly, just finishing with SETcc (a 0/1 value) instead of Jcc
         * (a jump). */
        {
            int is_cmp_op = (!strcmp(op,"==")||!strcmp(op,"!=")||!strcmp(op,"<")||
                              !strcmp(op,"<=")||!strcmp(op,">")||!strcmp(op,">="));
            if (is_cmp_op && cg->is_64bit) {
                int lhs_is_float = codegen_is_float_expr(cg, n->binary.left);
                int rhs_is_float = codegen_is_float_expr(cg, n->binary.right);
                if (lhs_is_float || rhs_is_float) {
                    if (lhs_is_float) codegen_float_expr(cg, n->binary.left);
                    else { codegen_expr(cg, n->binary.left); asm_cvtsi2sd(a, 0, REG_RAX); }
                    asm_sub_rsp(a,16);
                    asm_emit4(a,0xF2,0x0F,0x11,0x04); asm_emit1(a,0x24); /* movsd [rsp],xmm0 */
                    if (rhs_is_float) codegen_float_expr(cg, n->binary.right);
                    else { codegen_expr(cg, n->binary.right); asm_cvtsi2sd(a, 0, REG_RAX); }
                    asm_movsd_xmm(a,1,0);                                /* xmm1 = rhs */
                    asm_emit4(a,0xF2,0x0F,0x10,0x04); asm_emit1(a,0x24); /* movsd xmm0,[rsp] = lhs */
                    asm_add_rsp(a,16);
                    asm_ucomisd(a,0,1);
                    CondCode fcc;
                    if      (!strcmp(op,"==")) fcc = CC_E;
                    else if (!strcmp(op,"!=")) fcc = CC_NE;
                    else if (!strcmp(op,"<"))  fcc = CC_B;
                    else if (!strcmp(op,"<=")) fcc = CC_BE;
                    else if (!strcmp(op,">"))  fcc = CC_A;
                    else                        fcc = CC_AE;
                    asm_setcc_al(a,fcc); asm_movzx_rax_al(a);
                    break;
                }
            }
        }
        /* Pointer arithmetic scaling: "ptr + i" / "ptr - i" / "ptr - ptr"
         * (checked before evaluating either side — these are just symbol
         * lookups, no codegen emitted yet). See pointer_pointee_size()'s
         * comment for why this matters (a real, general squash gap, not
         * specific to "+"/"-" — array-index syntax "arr[i]" already scaled
         * correctly via elem_size_of(); only the "ptr + i" spelling of the
         * exact same arithmetic didn't). */
        int lp_sz = (strcmp(op,"+")==0||strcmp(op,"-")==0) ? pointer_pointee_size(cg, n->binary.left)  : 0;
        int rp_sz = (strcmp(op,"+")==0||strcmp(op,"-")==0) ? pointer_pointee_size(cg, n->binary.right) : 0;
        codegen_expr(cg,n->binary.left);  asm_push_reg(a,REG_RAX);
        codegen_expr(cg,n->binary.right); asm_pop_reg(a,REG_RBX);
        /* RBX=left, RAX=right */
        if (strcmp(op,"+")==0) {
            if (lp_sz>0 && rp_sz==0) { asm_mov_reg_imm(a,REG_RCX,(long long)lp_sz); asm_imul_reg_reg(a,REG_RAX,REG_RCX); }
            else if (rp_sz>0 && lp_sz==0) { asm_mov_reg_imm(a,REG_RCX,(long long)rp_sz); asm_imul_reg_reg(a,REG_RBX,REG_RCX); }
            asm_add_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX);
        }
        else if (strcmp(op,"-")==0) {
            if (lp_sz>0 && rp_sz>0) {
                /* Pointer difference: (left-right) raw bytes / element size. */
                asm_sub_reg_reg(a,REG_RBX,REG_RAX);
                asm_mov_reg_imm(a,REG_RCX,(long long)lp_sz);
                asm_mov_reg_reg(a,REG_RAX,REG_RBX);
                asm_idiv_reg(a,REG_RCX); /* emits cqo/cdq internally before dividing */
            } else {
                if (lp_sz>0 && rp_sz==0) { asm_mov_reg_imm(a,REG_RCX,(long long)lp_sz); asm_imul_reg_reg(a,REG_RAX,REG_RCX); }
                asm_sub_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX);
            }
        }
        else if (strcmp(op,"*")==0)  { asm_imul_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"/")==0)  { asm_mov_reg_reg(a,REG_RCX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX);
            int uns = expr_is_unsigned_int(cg,n->binary.left) || expr_is_unsigned_int(cg,n->binary.right);
            if (uns) asm_div_reg(a,REG_RCX); else asm_idiv_reg(a,REG_RCX); }
        else if (strcmp(op,"%")==0)  { asm_mov_reg_reg(a,REG_RCX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX);
            int uns = expr_is_unsigned_int(cg,n->binary.left) || expr_is_unsigned_int(cg,n->binary.right);
            if (uns) asm_div_reg(a,REG_RCX); else asm_idiv_reg(a,REG_RCX);
            asm_mov_reg_reg(a,REG_RAX,REG_RDX); }
        else if (strcmp(op,"&")==0)  { asm_and_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"|")==0)  { asm_or_reg_reg (a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"^")==0)  { asm_xor_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"<<")==0) { asm_mov_reg_reg(a,REG_RCX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); asm_shl_reg_cl(a,REG_RAX); }
        else if (strcmp(op,">>")==0) { asm_mov_reg_reg(a,REG_RCX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); asm_shr_reg_cl(a,REG_RAX); }
        else {
            asm_cmp_reg_reg(a,REG_RBX,REG_RAX);
            /* Relational comparisons (<, >, <=, >=) previously always used
             * SIGNED condition codes (JL/JG/JLE/JGE) regardless of operand
             * signedness — harmless for small positive values, but wrong
             * for an unsigned value with the high bit set: e.g. comparing
             * an unsigned 0xFFFFFFFFFFFFFFFF (all bits set) against 0 with
             * a signed condition code treats it as -1, so "value > 0"
             * wrongly came out false. This broke every SDL_ulltoa()/
             * SDL_lltoa()-style "while (value > 0) { ...; value /= radix;
             * }" digit-extraction loop for any unsigned 64-bit value >=
             * 2^63 — the loop body never ran at all, producing an empty
             * string (the digit VALUES from "%"/"/" were already correct
             * by this point via the sibling asm_div_reg fix above; the
             * loop just never entered). ==/!= are unaffected (equality
             * doesn't depend on sign interpretation). */
            int uns = strcmp(op,"==")!=0 && strcmp(op,"!=")!=0 &&
                      (expr_is_unsigned_int(cg,n->binary.left) || expr_is_unsigned_int(cg,n->binary.right));
            CondCode cc;
            if      (strcmp(op,"==")==0) cc=CC_E;
            else if (strcmp(op,"!=")==0) cc=CC_NE;
            else if (strcmp(op,"<" )==0) cc = uns ? CC_B  : CC_L;
            else if (strcmp(op,">" )==0) cc = uns ? CC_A  : CC_G;
            else if (strcmp(op,"<=")==0) cc = uns ? CC_BE : CC_LE;
            else                          cc = uns ? CC_AE : CC_GE;
            asm_setcc_al(a,cc); asm_movzx_rax_al(a);
        }
        break;
    }

    case AST_TERNARY: {
        int else_lbl=asm_new_label(a,"tern_else"), end_lbl=asm_new_label(a,"tern_end");
        codegen_expr(cg,n->ternary.cond);
        asm_test_reg_reg(a,REG_RAX,REG_RAX);
        asm_jcc_label(a,CC_E,else_lbl);
        codegen_expr(cg,n->ternary.then_);
        asm_jmp_label(a,end_lbl);
        asm_def_label(a,else_lbl);
        codegen_expr(cg,n->ternary.else_);
        asm_def_label(a,end_lbl);
        break;
    }

    case AST_CALL: {
        const char *name=n->call.name;
        Symbol *sym=symtable_lookup(cg->sym,name);

        /* alloca(n) is a compiler intrinsic, not a real exported function —
         * msvcrt.dll has no symbol literally named "alloca", so routing it
         * through the generic "unknown function -> msvcrt.dll" import
         * fallback produces an unresolvable import that either fails to
         * load at all or crashes on first use. Real alloca must grow the
         * CALLER's own stack frame by moving RSP down at runtime; this is
         * safe here because every function epilogue uses `leave` (mov
         * rsp,rbp; pop rbp), which restores RSP from RBP regardless of any
         * such mid-function adjustment. */
        if (cg->is_64bit && n->call.argc==1 && strcmp(name,"alloca")==0 && !(sym && sym->kind==SYM_FUNC && sym->func_node && sym->func_node->func.body)) {
            codegen_expr(cg, n->call.args[0]);            /* RAX = requested size */
            asm_mov_reg_imm(a, REG_RCX, 15);
            asm_add_reg_reg(a, REG_RAX, REG_RCX);         /* size+15 */
            asm_mov_reg_imm(a, REG_RCX, ~(long long)15);
            asm_and_reg_reg(a, REG_RAX, REG_RCX);         /* round down to 16 */
            asm_sub_reg_reg(a, REG_RSP, REG_RAX);         /* grow stack down  */
            asm_mov_reg_reg(a, REG_RAX, REG_RSP);         /* return the new top */
            break;
        }

        if (is_internal_shim(name)) {
            int _snrs = 0;
            if (!cg->is_64bit) {
                _snrs = (!strcmp(name,"strcpy")||!strcmp(name,"strcmp")||!strcmp(name,"strncmp")||
                    !strcmp(name,"strcat")||!strcmp(name,"strncat")||!strcmp(name,"strchr")||!strcmp(name,"strstr")||
                    !strcmp(name,"strncpy")||!strcmp(name,"atoi")||!strcmp(name,"atol")||!strcmp(name,"memcmp")||
                    !strcmp(name,"memcpy")||!strcmp(name,"memmove")||!strcmp(name,"memset"));
            }
            if (_snrs) {
                /* Save callee-preserved registers that shims may clobber in 32-bit */
                asm_push_reg(a, REG_EBX);
                asm_push_reg(a, REG_ESI);
                asm_push_reg(a, REG_EDI);
                emit_internal_call(cg,name,n->call.args,n->call.argc);
                /* Result in EAX. Restore regs without disturbing EAX. */
                asm_pop_reg(a, REG_EDI);
                asm_pop_reg(a, REG_ESI);
                asm_pop_reg(a, REG_EBX);
            } else {
                emit_internal_call(cg,name,n->call.args,n->call.argc);
            }
            break;
        }

        /* If the symbol is a variable, parameter, or global (not a declared
         * function), treat it as a function pointer: load the pointer value
         * and call through it. This handles: int (*op)(int,int); op = fp_add; op(1,2);
         * as well as global function-pointer tables (e.g. GL/GLX extension
         * loaders storing pfnGl* pointers in globals and calling them by name).
         * Resolve typedef'd pointer types too — a `typedef R (*T)(...); T g;`
         * global's own TypeInfo has base="T", pointer_depth==0 as literally
         * written; the pointer_depth lives on T's typedef entry instead. */
        TypeInfo *fptr_check_type = (sym && sym->type) ? sym->type : NULL;
        if (fptr_check_type && fptr_check_type->pointer_depth==0 && fptr_check_type->base) {
            Symbol *fptd = symtable_lookup(cg->sym, fptr_check_type->base);
            if (fptd && fptd->kind==SYM_TYPEDEF && fptd->type)
                fptr_check_type = fptd->type;
        }
        int is_fptr_var = (sym &&
                          (sym->kind==SYM_VAR || sym->kind==SYM_PARAM || sym->kind==SYM_GLOBAL) &&
                           fptr_check_type && fptr_check_type->pointer_depth > 0);
        if (is_fptr_var) {
            /* Build an AST_FUNC_PTR_CALL and evaluate it */
            ASTNode fake_var; memset(&fake_var,0,sizeof fake_var);
            fake_var.kind=AST_VAR; fake_var.line=n->line;
            fake_var.var.name=(char*)name;
            ASTNode fake_fpcall; memset(&fake_fpcall,0,sizeof fake_fpcall);
            fake_fpcall.kind=AST_FUNC_PTR_CALL; fake_fpcall.line=n->line;
            fake_fpcall.fp_call.func_expr=&fake_var;
            fake_fpcall.fp_call.args=n->call.args;
            fake_fpcall.fp_call.argc=n->call.argc;
            codegen_expr(cg,&fake_fpcall);
            break;
        }

        /* No symbol at all for this call target — not a locally-defined
         * function, not a variable/parameter/global holding a function
         * pointer, not alloca, not one of the internal CRT shims (all
         * ruled out above, each via its own early `break` if it applied).
         * Real C would call this an "implicit declaration" (every genuine
         * external call this codebase makes — Win32, Vulkan, OpenCL, the
         * CRT — has a real prototype in some #included header, which
         * registers a symbol table entry of its own long before codegen
         * ever runs; see codegen_program()'s note on parsing the whole
         * file before any codegen starts, so even a same-file FORWARD
         * reference to a function defined later is already resolved by
         * here). This does NOT change how the call itself resolves (still
         * falls through to the exact same DLL-import-guessing codegen
         * below) — it's diagnostic-only, so a genuine typo of a function
         * name gets the same "did you mean" treatment as a typo'd
         * variable, instead of silently compiling into a bogus import that
         * only fails at load time (or worse, coincidentally resolves to
         * some unrelated real DLL export and misbehaves at runtime). */
        if (!sym) {
            char sugbuf[160] = "";
            const char *suggestion = diag_suggest_name(cg->sym, name);
            if (suggestion) snprintf(sugbuf, sizeof sugbuf, " (did you mean '%s'?)", suggestion);
            diag_emit(DIAG_WARNING, n->line, cg->cur_func_name, name,
                      "implicit declaration of function '%s'%s -- no prototype in scope; treating it as an external call",
                      name, sugbuf);
        }

        int argc=n->call.argc;
        if (cg->is_64bit && cg->is_linux) {
            /* Linux SysV AMD64: int args in RDI,RSI,RDX,RCX,R8,R9;
             * float/double args in XMM0-XMM7 (independent register banks) */
            int frame;
            if (call_has_large_struct_arg(cg, n->call.args, argc)) {
                /* See classify_sysv_call_args's own comment for why this is
                 * a separate path, only taken when a struct/union-by-value
                 * argument bigger than 16 bytes is actually present. */
                int mem_size[64], is_float_a[64], uses_reg[64], reg_index[64], scratch_slot[64], stack_slot[64];
                int nregtotal, nstack;
                int cargc = argc > 64 ? 64 : argc;
                classify_sysv_call_args(cg, n->call.args, cargc, mem_size, is_float_a, uses_reg,
                                         reg_index, scratch_slot, stack_slot, &nregtotal, &nstack);
                int reg_scratch_base = nstack * 8;
                frame = reg_scratch_base + nregtotal * 8;
                frame = (frame + 15) & ~15;
                if (frame < 16) frame = 16;
                asm_sub_rsp(a, frame);
                emit_sysv_struct_call_args(cg, n->call.args, cargc, mem_size, is_float_a, uses_reg,
                                            reg_index, scratch_slot, stack_slot, reg_scratch_base, sym);
            } else {
            int nreg = argc < 6 ? argc : 6;
            int nextra = argc > 6 ? argc - 6 : 0;
            frame = (argc > 0 ? argc : 1) * 8;
            frame = (frame + 15) & ~15;
            if (frame < 16) frame = 16;
            asm_sub_rsp(a, frame);
            int scratch = nextra * 8;
            int arg_is_float[6] = {0,0,0,0,0,0};
            {
                int _i = 0;
                while (_i < nreg) {
                    int af = codegen_is_float_expr(cg, n->call.args[_i]);
                    arg_is_float[_i] = af;
                    if (af) { codegen_float_expr(cg, n->call.args[_i]); asm_movsd_store(a, REG_RSP, scratch+_i*8, 0); }
                    else    { codegen_expr(cg, n->call.args[_i]); asm_mov_mem_reg(a, REG_RSP, scratch+_i*8, REG_RAX); }
                    _i++;
                }
                _i = 0;
                while (_i < nextra) {
                    /* Stack-passed overflow args (7th+ position) must be
                     * type-checked the same way the first 6 (register-
                     * bound) args are just above -- a float/double here
                     * evaluated via the plain int path (codegen_expr) reads
                     * back as garbage in the callee (confirmed via a
                     * standalone 9-float-parameter repro: params 7-9 came
                     * back as 0.0 instead of their real values). Mirrors
                     * the nreg loop's own float/int split -- INCLUDING the
                     * single-precision narrow: a "float"-typed (not
                     * "double") parameter is read back by the callee via
                     * a 4-byte movss at its stack slot (codegen_float_expr's
                     * own AST_VAR case, keyed off the declared type's
                     * size), not the 8-byte movsd used to get the value
                     * there -- storing the full double bit pattern without
                     * narrowing first leaves the movss read seeing the
                     * DOUBLE's low 4 bytes, which are exactly zero for any
                     * "round" value (confirmed: 7.0/70.0 both reproduced as
                     * exactly 0.0 in the callee before this narrow was
                     * added). */
                    int af = codegen_is_float_expr(cg, n->call.args[6+_i]);
                    if (af) {
                        codegen_float_expr(cg, n->call.args[6+_i]);
                        if (param_is_single_float(sym, 6+_i, cg->sym, cg->is_64bit)) asm_cvtsd2ss(a,0,0);
                        asm_movsd_store(a, REG_RSP, _i*8, 0);
                    }
                    else    { codegen_expr(cg, n->call.args[6+_i]); asm_mov_mem_reg(a, REG_RSP, _i*8, REG_RAX); }
                    _i++;
                }
            }
            /* Load args into correct SysV registers: floats→xmm, ints→rdi/rsi/rdx/rcx/r8/r9
             * For single-precision (float) params: cvtsd2ss double→float before passing.
             * This applies to both external and squash-internal calls: the callee prologue
             * uses movsd to store float params, so the float value must be in xmm's lower 32 bits. */
            {
#define CALL_IREG(i) ((i)==0?REG_RDI:(i)==1?REG_RSI:(i)==2?REG_RDX:(i)==3?REG_RCX:(i)==4?REG_R8:REG_R9)
                int ireg = 0, freg = 0;
                int _k;
                for (_k = 0; _k < nreg; _k++) {
                    if (arg_is_float[_k]) {
                        asm_movsd_load(a, freg, REG_RSP, scratch + _k*8);
                        if (param_is_single_float(sym, _k, cg->sym, cg->is_64bit))
                            asm_cvtsd2ss(a, freg, freg);
                        freg++;
                    } else {
                        asm_mov_reg_mem(a, CALL_IREG(ireg), REG_RSP, scratch + _k*8);
                        ireg++;
                    }
                }
#undef CALL_IREG
            }
            }
            if (sym && sym->kind==SYM_IMPORT && sym->dll && strcmp(sym->dll,"extern")==0 && cg->is_linux) {
                /* dll=="extern" is a sentinel the parser uses for plain
                 * "extern <decl>;" declarations -- meaningless as an ELF
                 * import-DLL name, so it gets silently dropped elsewhere
                 * (elf_grp_add), leaving the call's GOT slot never created
                 * and the call unresolved at runtime with no compile-time
                 * error (confirmed: "extern long write(...);" + a call
                 * segfaulted this way). On Linux this means "look it up as
                 * a real libc.so.6 export, or a cross-object ".sqo"/".a"
                 * symbol if this build is linking one in" -- same
                 * resolution chain as a plain (non-"extern"-keyword)
                 * bodyless SYM_FUNC declaration a few branches below,
                 * mirrored here since the parser's "extern" convention
                 * makes this land in SYM_IMPORT instead of SYM_FUNC. */
                if (cg->linker) {
                    const char *ext_soname = linker_lookup_dynamic(cg->linker, name);
                    if (ext_soname) {
                        char extk[512]; snprintf(extk,sizeof extk,"%s:%s",ext_soname,name);
                        symtable_add_import(cg->sym,extk);
                        asm_call_import(a,name);
                    } else if (linker_link_static(cg->linker, name)) {
                        asm_call_static(a,name);
                    } else {
                        emit_linux_libc_call(cg, name, n->call.args, argc);
                    }
                } else if (cg->prefer_static_calls && codegen_is_sqo_export(cg,name)) {
                    asm_call_static(a,name);
                } else {
                    emit_linux_libc_call(cg, name, n->call.args, argc);
                }
            } else if (sym && sym->kind==SYM_IMPORT) {
                char lkey[512]; snprintf(lkey,sizeof lkey,"%s:%s",sym->dll,name);
                symtable_add_import(cg->sym,lkey);
                asm_call_import(a,name);
            } else if (sym && sym->kind==SYM_FUNC) {
                /* symtable_find_dll's table (KERNEL32/opengl32/gdi32/user32)
                 * is Windows-only; on Linux "glClearColor" etc. must resolve
                 * through the libGL.so path below instead, not get treated
                 * as needing a Windows "opengl32.dll" import. */
                const char *ldll = cg->is_linux ? NULL : symtable_find_dll(cg->sym, name);
                if (ldll) {
                    char lkey2[512]; snprintf(lkey2,sizeof lkey2,"%s:%s",ldll,name);
                    symtable_add_import(cg->sym,lkey2);
                    asm_call_import(a,name);
                } else if ((sym->func_node == NULL || sym->func_node->func.body == NULL) && cg->linker) {
                    const char *soname = linker_lookup_dynamic(cg->linker, name);
                    if (soname) {
                        char lk[512]; snprintf(lk,sizeof lk,"%s:%s",soname,name);
                        symtable_add_import(cg->sym,lk);
                        asm_call_import(a,name);
                    } else if (linker_link_static(cg->linker, name)) {
                        asm_call_static(a,name);
                    } else {
                        /* See emit_linux_libc_call's own comment (near its
                         * definition) for why this must go through it
                         * rather than a bare symtable_add_import +
                         * asm_call_import here: the latter, reached this
                         * deep inside the generic SysV call-emission path,
                         * was confirmed to segfault for a real, working
                         * libc.so.6 symbol (e.g. plain "write(2,buf,n)")
                         * despite registering an identical "libc.so.6:name"
                         * import spec — emit_linux_libc_call is squash's
                         * one proven-working mechanism for this. */
                        emit_linux_libc_call(cg, name, n->call.args, argc);
                    }
                } else if ((sym->func_node == NULL || sym->func_node->func.body == NULL) && cg->prefer_static_calls &&
                           (cg->sqo_precompile || codegen_is_sqo_export(cg,name))) {
                    /* Declared but not locally defined in this TU. Two ways
                     * to land here (see cg->sqo_precompile's own comment in
                     * codegen.h for the full reasoning):
                     *  - This IS a "-c" precompile (sqo_precompile): there
                     *    is no sibling object to check yet (that's the
                     *    entire point of separate compilation), so a plain
                     *    bodyless SYM_FUNC name is speculatively assumed to
                     *    be a same-program helper resolved later — exactly
                     *    the case objfile.h's cross-object model exists
                     *    for (confirmed via squash self-hosting itself:
                     *    lexer.c's bare "my_strdup(...)" prototype, defined
                     *    only in ast.c, needs exactly this to ever link).
                     *  - This is the final merge step (n_obj>0) and `name`
                     *    is confirmed among the ".sqo" files' real exports
                     *    (codegen_is_sqo_export) — prefer_static_calls
                     *    alone used to be treated as sufficient here, which
                     *    broke a plain "extern long write(...)" call the
                     *    moment ANY ".sqo" was linked in, regardless of
                     *    whether write() was actually among its exports.
                     * Emit a named, PC-relative deferred call
                     * (RELOC_STATIC_REL32) instead of a local direct-call
                     * label fixup -- the latter hard-fails at asm_resolve()
                     * ("undefined label") since no body exists here, and
                     * for the genuinely-undefined case degrades to the same
                     * soft "unresolved static symbol" warning every other
                     * unresolved cross-module reference already gets. */
                    asm_call_static(a,name);
                } else if (sym->func_node == NULL || sym->func_node->func.body == NULL) {
                    /* See emit_linux_libc_call's own comment for why this
                     * must go through it rather than a bare
                     * symtable_add_import + asm_call_import here. */
                    emit_linux_libc_call(cg, name, n->call.args, argc);
                } else {
                    asm_call_direct(a,get_func_label(cg,name));
                }
            } else {
                /* See emit_linux_libc_call's own comment for why this must
                 * go through it rather than a bare symtable_add_import +
                 * asm_call_import here. */
                emit_linux_libc_call(cg, name, n->call.args, argc);
            }
            asm_add_rsp(a, frame);
        } else if (cg->is_64bit) {
            /* Shadow space (32) + extra for args>=5 */
            int extra = argc > 4 ? (argc-4)*8 : 0;
            int frame = 32 + extra;
            if ((frame&8)==0) frame+=8;  /* RSP must be 0-mod-16 at CALL */
            /* Evaluate args BEFORE allocating shadow space.
             * On Windows x64 there is NO red zone - writing below RSP via PUSH
             * is unsafe (OS uses that memory). So evaluate args into registers
             * first, THEN allocate shadow space, then call. */
            {
                /* Evaluate args directly into ABI registers (no store-load intermediary).
                 * Evaluate in forward order; each arg goes straight to its register.
                 * This eliminates the repetitive [rsp+32]/[rsp+40] load pattern. 
                 * Stack args (argc>=5) are stored to [rsp+32+(i-4)*8] after regs are set. */
                {
                    /* First evaluate args 0..3 directly into registers */
                    if (argc>=1) {
                        int af=codegen_is_float_expr(cg,n->call.args[0]);
                        if(af){
                            codegen_float_expr(cg,n->call.args[0]);
                            /* codegen_float_expr always computes in double precision;
                             * narrow to a real 32-bit float before the callee reads it
                             * as one — see AST_RETURN's identical fix for the full story
                             * (this is the same bug, just at a call site instead of a
                             * return: e.g. SDL_SetRenderDrawColorFloat(renderer, fR, ...)
                             * silently corrupted every color channel it was called with). */
                            if (param_is_single_float(sym, 0, cg->sym, cg->is_64bit)) asm_cvtsd2ss(a,0,0);
                            /* Win64 variadic ABI: duplicate into RCX too — see
                             * asm_movq_gpr_from_xmm()'s own comment for why. */
                            asm_movq_gpr_from_xmm(a, REG_RCX, 0);
                        }
                        else{codegen_expr(cg,n->call.args[0]); asm_mov_reg_reg(a,REG_RCX,REG_RAX);}
                    }
                    if (argc>=2) {
                        /* Save RCX (arg0) on stack if arg1 contains a nested call
                         * (nested call clobbers volatile registers including RCX) */
                        int arg1_has_call = expr_has_call(n->call.args[1]);
                        int arg1_float = codegen_is_float_expr(cg,n->call.args[1]);
                        int arg0_float = codegen_is_float_expr(cg,n->call.args[0]);
                        if(arg1_has_call && !arg0_float && !arg1_float)
                            asm_push_reg(a,REG_RCX); /* save RCX (arg0) on stack */
                        if(arg1_float){
                            /* If arg0 was ALSO a float, it's currently resident in
                             * XMM0 — its permanent home for position 0 — but
                             * codegen_float_expr() unconditionally uses XMM0 as its
                             * own result/scratch register, silently clobbering
                             * arg0's value before it's ever moved anywhere else.
                             * This is exactly the shape of libm's own kernel
                             * functions (e.g. __kernel_sin(double x, double y, int
                             * iy)) — two doubles in a row — which is why every
                             * SDL_sin/SDL_cos call silently returned the wrong
                             * value (the 2nd+ argument, or worse, both ended up
                             * holding the LAST float argument's value) regardless
                             * of the earlier extern-declaration float-marshalling
                             * fix, which only covers a DIFFERENT bug class. Save
                             * arg0's XMM0 to the stack before clobbering it, then
                             * restore once arg1 is safely in XMM1. */
                            if (arg0_float) { asm_sub_rsp(a,8); asm_movsd_store(a,REG_RSP,0,0); }
                            codegen_float_expr(cg,n->call.args[1]);
                            if (param_is_single_float(sym, 1, cg->sym, cg->is_64bit)) asm_cvtsd2ss(a,0,0);
                            /* Win64 ABI: float at position 1 must be in XMM1, not XMM0 */
                            asm_movsd_xmm(a,1,0); /* movsd xmm1, xmm0 */
                            /* Win64 variadic ABI: duplicate into RDX too. */
                            asm_movq_gpr_from_xmm(a, REG_RDX, 1);
                            if (arg0_float) { asm_movsd_load(a,0,REG_RSP,0); asm_add_rsp(a,8); }
                        }
                        else{codegen_expr(cg,n->call.args[1]); asm_mov_reg_reg(a,REG_RDX,REG_RAX);}
                        if(arg1_has_call && !arg0_float && !arg1_float)
                            asm_pop_reg(a,REG_RCX); /* restore RCX (arg0) from stack */
                    }
                    if (argc>=3) {
                        /* Save RCX (arg0) and RDX (arg1) if arg2 contains a nested call */
                        int arg2_has_call = expr_has_call(n->call.args[2]);
                        int af=codegen_is_float_expr(cg,n->call.args[2]);
                        if(arg2_has_call && !af) {
                            asm_push_reg(a,REG_RDX); /* save RDX (arg1) */
                            asm_push_reg(a,REG_RCX); /* save RCX (arg0) */
                        }
                        if(af){
                            /* Protect any earlier float args already resident in
                             * XMM0/XMM1 from codegen_float_expr()'s own XMM0/XMM1
                             * scratch usage — see the identical, fuller comment at
                             * the position-1 case above for why this matters. */
                            int save0 = codegen_is_float_expr(cg,n->call.args[0]);
                            int save1 = codegen_is_float_expr(cg,n->call.args[1]);
                            if (save0) { asm_sub_rsp(a,8); asm_movsd_store(a,REG_RSP,0,0); }
                            if (save1) { asm_sub_rsp(a,8); asm_movsd_store(a,REG_RSP,0,1); }
                            codegen_float_expr(cg,n->call.args[2]);
                            if (param_is_single_float(sym, 2, cg->sym, cg->is_64bit)) asm_cvtsd2ss(a,0,0);
                            /* Win64 ABI: float at position 2 must be in XMM2 */
                            asm_movsd_xmm(a,2,0); /* movsd xmm2, xmm0 */
                            /* Win64 variadic ABI: duplicate into R8 too. */
                            asm_movq_gpr_from_xmm(a, REG_R8, 2);
                            if (save1) { asm_movsd_load(a,1,REG_RSP,0); asm_add_rsp(a,8); }
                            if (save0) { asm_movsd_load(a,0,REG_RSP,0); asm_add_rsp(a,8); }
                        }
                        else{codegen_expr(cg,n->call.args[2]); asm_mov_reg_reg(a,REG_R8,REG_RAX);}
                        if(arg2_has_call && !af) {
                            asm_pop_reg(a,REG_RCX); /* restore RCX (arg0) */
                            asm_pop_reg(a,REG_RDX); /* restore RDX (arg1) */
                        }
                    }
                    if (argc>=4) {
                        /* Save RCX (arg0), RDX (arg1), R8 (arg2) if arg3 contains a nested call */
                        int arg3_has_call = expr_has_call(n->call.args[3]);
                        int af=codegen_is_float_expr(cg,n->call.args[3]);
                        if(arg3_has_call && !af) {
                            asm_push_reg(a,REG_R8);  /* save R8 (arg2) */
                            asm_push_reg(a,REG_RDX); /* save RDX (arg1) */
                            asm_push_reg(a,REG_RCX); /* save RCX (arg0) */
                        }
                        if(af){
                            /* Protect any earlier float args already resident in
                             * XMM0/XMM1/XMM2 — see the position-1 case above. */
                            int save0 = codegen_is_float_expr(cg,n->call.args[0]);
                            int save1 = codegen_is_float_expr(cg,n->call.args[1]);
                            int save2 = codegen_is_float_expr(cg,n->call.args[2]);
                            if (save0) { asm_sub_rsp(a,8); asm_movsd_store(a,REG_RSP,0,0); }
                            if (save1) { asm_sub_rsp(a,8); asm_movsd_store(a,REG_RSP,0,1); }
                            if (save2) { asm_sub_rsp(a,8); asm_movsd_store(a,REG_RSP,0,2); }
                            codegen_float_expr(cg,n->call.args[3]);
                            if (param_is_single_float(sym, 3, cg->sym, cg->is_64bit)) asm_cvtsd2ss(a,0,0);
                            /* Win64 ABI: float at position 3 must be in XMM3 */
                            asm_movsd_xmm(a,3,0); /* movsd xmm3, xmm0 */
                            /* Win64 variadic ABI: duplicate into R9 too. */
                            asm_movq_gpr_from_xmm(a, REG_R9, 3);
                            if (save2) { asm_movsd_load(a,2,REG_RSP,0); asm_add_rsp(a,8); }
                            if (save1) { asm_movsd_load(a,1,REG_RSP,0); asm_add_rsp(a,8); }
                            if (save0) { asm_movsd_load(a,0,REG_RSP,0); asm_add_rsp(a,8); }
                        }
                        else{codegen_expr(cg,n->call.args[3]); asm_mov_reg_reg(a,REG_R9,REG_RAX);}
                        if(arg3_has_call && !af) {
                            asm_pop_reg(a,REG_RCX); /* restore RCX (arg0) */
                            asm_pop_reg(a,REG_RDX); /* restore RDX (arg1) */
                            asm_pop_reg(a,REG_R8);  /* restore R8 (arg2) */
                        }
                    }
                }
            }
            /* Now allocate shadow space - AFTER args are in registers */
            asm_sub_rsp(a,frame);
            /* Stack args >= 4: store after shadow is allocated */
            for(int i=4;i<argc;i++) {
                int af=codegen_is_float_expr(cg,n->call.args[i]);
                if(af){
                    codegen_float_expr(cg,n->call.args[i]);
                    if (param_is_single_float(sym, i, cg->sym, cg->is_64bit)) asm_cvtsd2ss(a,0,0);
                    asm_movsd_store(a,REG_RSP,32+(i-4)*8,0);
                }
                else{codegen_expr(cg,n->call.args[i]); asm_mov_mem_reg(a,REG_RSP,32+(i-4)*8,REG_RAX);}
            }
            if (sym && sym->kind==SYM_IMPORT && sym->dll && strcmp(sym->dll,"extern")==0 && cg->is_linux) {
                /* dll=="extern" is a sentinel the parser uses for plain
                 * "extern <decl>;" declarations -- meaningless as an ELF
                 * import-DLL name, so it gets silently dropped elsewhere,
                 * leaving the call's GOT slot never created and the call
                 * unresolved at runtime with no compile-time error. On
                 * Linux this means "look it up as a real libc.so.6 export,
                 * or a cross-object .sqo/.a symbol if this build is linking
                 * one in" -- see the identical fix (and its own longer
                 * comment) on the equivalent branch earlier in this
                 * function. */
                if (cg->linker) {
                    const char *ext_soname2 = linker_lookup_dynamic(cg->linker, name);
                    if (ext_soname2) {
                        char extk2[512]; snprintf(extk2,sizeof extk2,"%s:%s",ext_soname2,name);
                        symtable_add_import(cg->sym,extk2);
                        asm_call_import(a,name);
                    } else if (linker_link_static(cg->linker, name)) {
                        asm_call_static(a,name);
                    } else {
                        emit_linux_libc_call(cg, name, n->call.args, argc);
                    }
                } else if (cg->prefer_static_calls && codegen_is_sqo_export(cg,name)) {
                    asm_call_static(a,name);
                } else {
                    emit_linux_libc_call(cg, name, n->call.args, argc);
                }
            } else if (sym && sym->kind==SYM_IMPORT && sym->dll && strcmp(sym->dll,"extern")==0 &&
                !symtable_find_dll(cg->sym, name)) {
                /* parser_new4.c's ParseFunction registers ANY bodyless
                 * "extern" forward declaration as a SYM_IMPORT with the
                 * literal placeholder dll name "extern" (not a real DLL) —
                 * a pre-existing convention from this compiler's original
                 * single-TU model, where a function declared-but-never-
                 * defined anywhere in the file could only be a real OS/DLL
                 * import. For separate compilation (squash -c) that's no
                 * longer the only possibility — it may be a function this
                 * object doesn't define but a sibling object (linked in
                 * later, see objfile.c) does. Route through the same
                 * deferred, name-carrying call as a genuine SYM_FUNC
                 * declaration below, instead of a doomed "import from a
                 * DLL called literally 'extern'". Windows-only branch (see
                 * the is_linux-gated branch above for Linux) — real Windows
                 * headers routinely declare genuine DLL-exported API
                 * functions via "EXTERN_C RET Name(...);" (EXTERN_C is
                 * literally "#define EXTERN_C extern" in plain C — see
                 * basetyps.h/guiddef.h/winnt.h — an extremely common idiom,
                 * especially throughout the COM-related headers), which is
                 * indistinguishable at parse time from this same "extern"
                 * placeholder. Without the symtable_find_dll check above,
                 * EVERY such declaration (e.g. CoInitializeEx,
                 * CoUninitialize) silently compiled to an unresolved direct
                 * call that was never patched (no sibling object exists to
                 * resolve it, since it's a real DLL export, not a cross-
                 * object symbol) — no error, just a broken call at runtime. */
                asm_call_static(a,name);
            } else if (sym && sym->kind==SYM_IMPORT && sym->dll && strcmp(sym->dll,"extern")==0) {
                const char *realdll = symtable_find_dll(cg->sym, name);
                char key[512]; snprintf(key,sizeof key,"%s:%s",realdll,name);
                symtable_add_import(cg->sym,key);
                asm_call_import(a,name);
            } else if (sym && sym->kind==SYM_IMPORT) {
                char key[512]; snprintf(key,sizeof key,"%s:%s",sym->dll,name);
                symtable_add_import(cg->sym,key);
                asm_call_import(a,name);
            } else if (sym && sym->kind==SYM_FUNC) {
                /* Check if it's a Windows API (declared extern but really an
                 * import) — Windows-only table, see comment at the other
                 * symtable_find_dll call site above. */
                const char *dll64 = cg->is_linux ? NULL : symtable_find_dll(cg->sym, name);
                if (dll64) {
                    char key64[512]; snprintf(key64,sizeof key64,"%s:%s",dll64,name);
                    symtable_add_import(cg->sym,key64);
                    asm_call_import(a,name);
                } else if ((sym->func_node == NULL || sym->func_node->func.body == NULL) && cg->is_linux && cg->linker) {
                    /* Declared but not locally defined — look in user libraries */
                    const char *soname = linker_lookup_dynamic(cg->linker, name);
                    if (soname) {
                        char lkey[512]; snprintf(lkey,sizeof lkey,"%s:%s",soname,name);
                        symtable_add_import(cg->sym,lkey);
                        asm_call_import(a,name);
                    } else if (linker_link_static(cg->linker, name)) {
                        asm_call_static(a,name);
                    } else {
                        /* See emit_linux_libc_call's own comment for why
                         * this must go through it rather than a bare
                         * symtable_add_import + asm_call_import here. */
                        emit_linux_libc_call(cg, name, n->call.args, argc);
                    }
                } else if (sym->func_node == NULL || sym->func_node->func.body == NULL) {
                    /* Declared (prototype seen — e.g. via an #include'd header)
                     * but with no body anywhere in THIS translation unit, and
                     * not a recognized Win32 DLL export. Previously this fell
                     * through to asm_call_direct() below, which is only safe
                     * because this compiler's usual model is "one giant unity
                     * TU" — a function declared-but-never-defined there is a
                     * genuine bug, and asm_resolve() catches it with a hard
                     * exit(1) ("undefined label"). For SEPARATE COMPILATION
                     * (squash -c, producing an object file later linked
                     * against another translation unit that DOES define this
                     * function — see objfile.c), that assumption no longer
                     * holds: the definition legitimately lives in a sibling
                     * object file, not this one. Emit a named, PC-relative
                     * deferred call (the same RELOC_STATIC_REL32 mechanism
                     * already used for pulling functions from a Linux .a
                     * archive) instead of a label-based direct call, so the
                     * object-file linker can resolve it later by name — and,
                     * for the plain single-TU case where this name is
                     * NEVER defined anywhere, the failure mode degrades from
                     * a compiler crash to the same soft "unresolved symbol"
                     * warning every other cross-module reference already
                     * gets, rather than aborting compilation outright. */
                    asm_call_static(a,name);
                } else {
                    /* Locally defined function: call directly */
                    asm_call_direct(a,get_func_label(cg,name));
                }
            } else {
                /* Unknown function: route through linker or libc/msvcrt fallback */
                if (cg->is_linux && cg->linker) {
                    const char *soname64 = linker_lookup_dynamic(cg->linker, name);
                    if (soname64) {
                        char lk64[512]; snprintf(lk64,sizeof lk64,"%s:%s",soname64,name);
                        symtable_add_import(cg->sym,lk64);
                        asm_call_import(a,name);
                    } else if (linker_link_static(cg->linker, name)) {
                        asm_call_static(a,name);
                    } else {
                        /* See emit_linux_libc_call's own comment for why
                         * this must go through it rather than a bare
                         * symtable_add_import + asm_call_import here. */
                        emit_linux_libc_call(cg, name, n->call.args, argc);
                    }
                } else if (cg->is_linux) {
                    emit_linux_libc_call(cg, name, n->call.args, argc);
                } else {
                    /* Genuinely undeclared identifier (parser never saw a
                     * prototype) — still check the known-WinAPI DLL table
                     * before defaulting to msvcrt.dll, same as the SYM_FUNC
                     * branch above, so real KERNEL32/user32/etc. exports
                     * don't get misrouted to a DLL that doesn't export them. */
                    const char *udll = symtable_find_dll(cg->sym, name);
                    const char *iname = udll ? name : win_msvcrt_import_alias(name);
                    char key[512];
                    snprintf(key,sizeof key,"%s:%s", udll ? udll : "msvcrt.dll", iname);
                    symtable_add_import(cg->sym,key);
                    asm_call_import(a,iname);
                }
            }
            asm_add_rsp(a,frame);
            /* Sign-extend EAX->RAX so that signed int return values compare correctly
             * in 64-bit (e.g. strcmp returning -1 = 0xFFFFFFFF zero-extends to large
             * positive, which would make "strcmp(..)<0" fail). */
            /* Note: don't emit sign-extension after call; callers handle type explicitly */
        } else {
            /* 32-bit cdecl: push args right-to-left.
             * Float args are 8 bytes on stack; int args are 4 bytes. */
            int total_arg_bytes = 0;
            for (int i=argc-1;i>=0;i--) {
                int arg_is_float = codegen_is_float_expr(cg, n->call.args[i]);
                if (arg_is_float) {
                    /* Push 8-byte double onto stack */
                    codegen_float_expr(cg, n->call.args[i]); /* -> ST0 */
                    asm_sub_rsp(a, 8);
                    asm_fstp_mem64(a, REG_ESP, 0); /* fstp qword[esp] */
                    total_arg_bytes += 8;
                } else if (is_64bit_int_arg(cg, n->call.args[i])) {
                    push_64bit_int_arg(cg, n->call.args[i]);
                    total_arg_bytes += 8;
                } else {
                    codegen_expr(cg, n->call.args[i]);
                    asm_push_reg(a, REG_EAX);
                    total_arg_bytes += 4;
                }
            }
            if (sym && sym->kind==SYM_IMPORT && sym->dll && strcmp(sym->dll,"extern")==0 &&
                (cg->is_linux || !symtable_find_dll(cg->sym, name))) {
                /* See the 64-bit call path's identical check above: a
                 * bodyless "EXTERN_C RET Name(...);" declaration (EXTERN_C
                 * == plain "extern" in C mode) is indistinguishable at
                 * parse time from this compiler's own cross-object-extern
                 * placeholder, so a real Windows DLL export must be ruled
                 * out first before assuming cross-object linkage. */
                asm_call_static(a,name);
            } else if (sym && sym->kind==SYM_IMPORT && sym->dll && strcmp(sym->dll,"extern")==0) {
                const char *realdll32 = symtable_find_dll(cg->sym, name);
                char key[512]; snprintf(key,sizeof key,"%s:%s",realdll32,name);
                symtable_add_import(cg->sym,key);
                asm_call_import32(a,name);
            } else if (sym && sym->kind==SYM_IMPORT) {
                char key[512]; snprintf(key,sizeof key,"%s:%s",sym->dll,name);
                symtable_add_import(cg->sym,key);
                asm_call_import32(a,name);
            } else if (sym && sym->kind==SYM_FUNC) {
                /* Windows-only table, see comment at the first
                 * symtable_find_dll call site above. */
                const char *dll32 = cg->is_linux ? NULL : symtable_find_dll(cg->sym, name);
                if (dll32) {
                    char key32b[512]; snprintf(key32b,sizeof key32b,"%s:%s",dll32,name);
                    symtable_add_import(cg->sym,key32b);
                    asm_call_import32(a,name);
                } else if ((sym->func_node == NULL || sym->func_node->func.body == NULL) && cg->is_linux && cg->linker) {
                    /* Declared but not locally defined */
                    const char *sn32 = linker_lookup_dynamic(cg->linker, name);
                    if (sn32) {
                        char k32[512]; snprintf(k32,sizeof k32,"%s:%s",sn32,name);
                        symtable_add_import(cg->sym,k32);
                        asm_call_import32(a,name);
                    } else if (linker_link_static(cg->linker, name)) {
                        asm_call_static(a,name);
                    } else {
                        char k32b[512]; snprintf(k32b,sizeof k32b,"%s:%s",g_squash_libc_soname,name);
                        symtable_add_import(cg->sym,k32b);
                        asm_call_import32(a,name);
                    }
                } else {
                    asm_call_direct(a,get_func_label(cg,name));
                }
            } else {
                /* Unknown function: route through linker or libc/msvcrt fallback */
                if (cg->is_linux && cg->linker) {
                    const char *sn32x = linker_lookup_dynamic(cg->linker, name);
                    if (sn32x) {
                        char k32x[512]; snprintf(k32x,sizeof k32x,"%s:%s",sn32x,name);
                        symtable_add_import(cg->sym,k32x);
                        asm_call_import32(a,name);
                    } else if (linker_link_static(cg->linker, name)) {
                        asm_call_static(a,name);
                    } else {
                        char k32y[512]; snprintf(k32y,sizeof k32y,"%s:%s",g_squash_libc_soname,name);
                        symtable_add_import(cg->sym,k32y);
                        asm_call_import32(a,name);
                    }
                } else if (cg->is_linux) {
                    char key32l[512]; snprintf(key32l,sizeof key32l,"%s:%s",g_squash_libc_soname,name);
                    symtable_add_import(cg->sym,key32l);
                    asm_call_import32(a,name);
                } else {
                    const char *iname32 = win_msvcrt_import_alias(name);
                    char key32[512]; snprintf(key32,sizeof key32,"msvcrt.dll:%s",iname32);
                    symtable_add_import(cg->sym,key32);
                    asm_call_import32(a,iname32);
                }
            }
            if (total_arg_bytes>0) asm_add_rsp(a,total_arg_bytes);
        }
        break;
    }

    case AST_FUNC_PTR_CALL: {
        if (!n->fp_call.func_expr) { asm_mov_reg_imm(a,REG_RAX,0); break; }
        int argc = n->fp_call.argc;
        if (cg->is_64bit && cg->is_linux) {
            /* Linux SysV: int/pointer args in RDI,RSI,RDX,RCX,R8,R9;
             * float/double args in XMM0-XMM7 — SEPARATE register banks,
             * not a shared pool. Evaluating every argument through the
             * integer path and loading only into GP registers (as this
             * used to do) silently dropped float arguments: e.g.
             * glUniform1f(loc, angle)'s float value never reached XMM0,
             * so the driver read whatever was already there (usually
             * stale/zero) instead — an animated GL uniform like a
             * rotation angle never visibly changed as a result. (Doesn't
             * yet split the >6-total-args overflow-to-stack decision by
             * register class — same known limitation as
             * emit_linux_libc_call; not hit by any call in the current
             * test suite.) */
            if (call_has_large_struct_arg(cg, n->fp_call.args, argc)) {
                /* See classify_sysv_call_args's own comment for why this is
                 * a separate path, only taken when a struct/union-by-value
                 * argument bigger than 16 bytes is actually present — this
                 * is exactly the case objc_msgSend() needs for e.g.
                 * -[NSWindow initWithContentRect:styleMask:backing:defer:],
                 * whose NSRect argument is 4 doubles (32 bytes). */
                int mem_size[64], is_float_a[64], uses_reg[64], reg_index[64], scratch_slot[64], stack_slot[64];
                int nregtotal, nstack;
                int cargc = argc > 64 ? 64 : argc;
                classify_sysv_call_args(cg, n->fp_call.args, cargc, mem_size, is_float_a, uses_reg,
                                         reg_index, scratch_slot, stack_slot, &nregtotal, &nstack);
                int reg_scratch_base = nstack * 8;
                int fnptr_off = reg_scratch_base + nregtotal * 8;
                int frame = fnptr_off + 8;
                frame = (frame + 15) & ~15;
                if (frame < 16) frame = 16;
                asm_sub_rsp(a, frame);
                codegen_expr(cg, n->fp_call.func_expr);
                asm_mov_mem_reg(a, REG_RSP, fnptr_off, REG_RAX); /* save fn ptr */
                emit_sysv_struct_call_args(cg, n->fp_call.args, cargc, mem_size, is_float_a, uses_reg,
                                            reg_index, scratch_slot, stack_slot, reg_scratch_base, NULL);
                asm_mov_reg_mem(a, REG_RAX, REG_RSP, fnptr_off); /* reload fn ptr */
                asm_call_reg(a, REG_RAX);
                asm_add_rsp(a, frame);
            } else {
            int nreg = argc < 6 ? argc : 6;
            int nextra = argc > 6 ? argc - 6 : 0;
            int frame = (argc > 0 ? argc + 1 : 2) * 8; /* +1 slot for fn ptr */
            frame = (frame + 15) & ~15;
            if (frame < 16) frame = 16;
            asm_sub_rsp(a, frame);
            codegen_expr(cg, n->fp_call.func_expr);
            asm_mov_mem_reg(a, REG_RSP, frame-8, REG_RAX); /* save fn ptr at top of frame */
            int scratch = nextra * 8;
            int arg_is_float[6] = {0,0,0,0,0,0};
            {
                int _i = 0;
                while (_i < nreg) {
                    int af = codegen_is_float_expr(cg, n->fp_call.args[_i]);
                    arg_is_float[_i] = af;
                    if (af) { codegen_float_expr(cg, n->fp_call.args[_i]); asm_movsd_store(a, REG_RSP, scratch+_i*8, 0); }
                    else    { codegen_expr(cg, n->fp_call.args[_i]); asm_mov_mem_reg(a, REG_RSP, scratch+_i*8, REG_RAX); }
                    _i++;
                }
                _i = 0;
                while (_i < nextra) {
                    /* See the identical fix on the direct-call SysV path
                     * above -- same "extras always evaluated as int" bug,
                     * same float/int type check fix, for the function-
                     * pointer-call path. NOT applying the sibling fix's
                     * single-precision cvtsd2ss narrow here: unlike a
                     * direct call, there is no resolved function Symbol at
                     * an indirect call site to ask "is this declared
                     * 'float' vs 'double'" (n->fp_call.func_expr is an
                     * arbitrary expression, not a name), and this file's
                     * nreg loop just above has the identical gap already
                     * (stores every float register arg via 8-byte movsd
                     * with no narrow) -- so this stays internally
                     * consistent with the rest of this call path rather
                     * than narrowing only the overflow args and not the
                     * register ones. */
                    int af = codegen_is_float_expr(cg, n->fp_call.args[6+_i]);
                    if (af) { codegen_float_expr(cg, n->fp_call.args[6+_i]); asm_movsd_store(a, REG_RSP, _i*8, 0); }
                    else    { codegen_expr(cg, n->fp_call.args[6+_i]); asm_mov_mem_reg(a, REG_RSP, _i*8, REG_RAX); }
                    _i++;
                }
            }
            asm_mov_reg_mem(a, REG_RAX, REG_RSP, frame-8); /* reload fn ptr */
            {
#define FPCALL_IREG(i) ((i)==0?REG_RDI:(i)==1?REG_RSI:(i)==2?REG_RDX:(i)==3?REG_RCX:(i)==4?REG_R8:REG_R9)
                int ireg=0, freg=0, _k;
                for (_k=0; _k<nreg; _k++) {
                    if (arg_is_float[_k]) {
                        asm_movsd_load(a, freg, REG_RSP, scratch+_k*8);
                        if (float_expr_width(cg, n->fp_call.args[_k])==4) asm_cvtsd2ss(a, freg, freg);
                        freg++;
                    } else {
                        asm_mov_reg_mem(a, FPCALL_IREG(ireg), REG_RSP, scratch+_k*8);
                        ireg++;
                    }
                }
#undef FPCALL_IREG
            }
            asm_call_reg(a, REG_RAX);
            asm_add_rsp(a, frame);
            }
        } else if (cg->is_64bit) {
            /* Evaluate the function pointer and every argument into scratch
             * stack slots FIRST, then load into ABI registers, THEN call —
             * evaluating each argument straight into its final register (as
             * this used to do) is unsafe: evaluating a later argument can
             * clobber an earlier one already sitting in RCX/RDX/R8/R9 (the
             * callee-volatile registers codegen_expr freely uses as scratch
             * for nested subexpressions). This corrupted 5+-argument calls
             * through a function pointer, e.g. the DSA GL call
             * glVertexArrayVertexBuffer(vao, binding, buf, offset, stride)
             * — confirmed via Wine: the 5th arg's evaluation clobbered an
             * earlier register-bound arg, and the call jumped through a
             * bogus "pointer" that was actually leftover small-integer
             * argument data. Stack-bound args (5th+) already wrote to their
             * own distinct, never-reused offsets, so only the register-
             * bound args (0-3) needed the scratch treatment. */
            int nreg = argc < 4 ? argc : 4;
            int nextra = argc > 4 ? argc - 4 : 0;
            int reg_scratch_off = 32 + nextra*8; /* fn ptr + first 4 args scratch */
            int frame = reg_scratch_off + (nreg+1)*8;
            /* Match AST_CALL's convention: RSP is 8-mod-16 (not 0-mod-16) at
             * this point in a function body, so the call-frame size must
             * itself be 8-mod-16 to land back on 0-mod-16 at the `call`
             * instant — rounding to a clean 16-byte boundary (as this used
             * to do) left RSP misaligned by 8 bytes for the actual call,
             * which silently "worked" for most callees but crashed deep
             * inside ntdll/kernel32 whenever the callee (or something it
             * called) executed an SSE-aligned instruction expecting a
             * 16-byte-aligned stack — e.g. SDL_LogOutput called through a
             * function pointer (SDL_Log's real callback path), vs. calling
             * the very same function directly, which goes through
             * AST_CALL's correctly-8-mod-16 frame sizing instead. */
            frame = (frame + 15) & ~15;
            if ((frame & 8) == 0) frame += 8;
            if (frame < 40) frame = 40;
            asm_sub_rsp(a, frame);
            codegen_expr(cg, n->fp_call.func_expr);
            asm_mov_mem_reg(a, REG_RSP, reg_scratch_off + nreg*8, REG_RAX);
            /* Windows x64 ABI: whichever of the first 4 argument POSITIONS
             * is float/double must go in the matching XMM register
             * (XMM0-3), not RCX/RDX/R8/R9 — this loop used to route EVERY
             * one of the first 4 args through the integer registers
             * unconditionally, silently corrupting any function-pointer
             * call whose callee expects a float/double in one of those
             * positions (the callee reads garbage from an integer register
             * that was never loaded with anything meaningful for it).
             * Track per-position float-ness so the register-loading step
             * below can pick the right register file for each position. */
            int fpc_arg_is_float[4] = {0,0,0,0};
            for (int i = 0; i < nreg; i++) {
                fpc_arg_is_float[i] = codegen_is_float_expr(cg, n->fp_call.args[i]);
                codegen_expr(cg, n->fp_call.args[i]);
                asm_mov_mem_reg(a, REG_RSP, reg_scratch_off + i*8, REG_RAX);
            }
            for (int i = 0; i < nextra; i++) {
                int af = codegen_is_float_expr(cg, n->fp_call.args[4+i]);
                codegen_expr(cg, n->fp_call.args[4+i]);
                if (af && float_expr_width(cg, n->fp_call.args[4+i]) == 4) {
                    /* codegen_expr's float path always computes in double
                     * precision (raw bits moved into RAX via movq) — narrow
                     * to a real 32-bit float bit pattern in RAX before
                     * storing, or the callee's 4-byte `movss` read of this
                     * stack slot gets the low 32 bits of a DOUBLE's layout
                     * instead (0 for the low bits of very ordinary values
                     * like 1.0/2.0 — this is exactly what made
                     * SDL_RenderTextureRotated's scale_x/scale_y arrive as
                     * 0 via renderer->QueueCopyEx, a 10-argument
                     * function-pointer call with these as the 8th/9th
                     * args). Round-trip through XMM0 to reuse the existing
                     * cvtsd2ss instruction helper rather than duplicating
                     * its raw byte encoding here. */
                    asm_emit4(a,0x66,0x48,0x0F,0x6E); asm_emit1(a,0xC0); /* movq xmm0,rax */
                    asm_cvtsd2ss(a, 0, 0);
                    asm_emit4(a,0x66,0x48,0x0F,0x7E); asm_emit1(a,0xC0); /* movq rax,xmm0 (low 32 bits now the real float) */
                }
                asm_mov_mem_reg(a, REG_RSP, 32 + i*8, REG_RAX);
            }
            if (nreg >= 1) {
                if (fpc_arg_is_float[0]) asm_movsd_load(a, 0, REG_RSP, reg_scratch_off + 0);
                else asm_mov_reg_mem(a, REG_RCX, REG_RSP, reg_scratch_off + 0);
            }
            if (nreg >= 2) {
                if (fpc_arg_is_float[1]) asm_movsd_load(a, 1, REG_RSP, reg_scratch_off + 8);
                else asm_mov_reg_mem(a, REG_RDX, REG_RSP, reg_scratch_off + 8);
            }
            if (nreg >= 3) {
                if (fpc_arg_is_float[2]) asm_movsd_load(a, 2, REG_RSP, reg_scratch_off + 16);
                else asm_mov_reg_mem(a, REG_R8,  REG_RSP, reg_scratch_off + 16);
            }
            if (nreg >= 4) {
                if (fpc_arg_is_float[3]) asm_movsd_load(a, 3, REG_RSP, reg_scratch_off + 24);
                else asm_mov_reg_mem(a, REG_R9,  REG_RSP, reg_scratch_off + 24);
            }
            for (int i = 0; i < nreg; i++) {
                if (fpc_arg_is_float[i] && float_expr_width(cg, n->fp_call.args[i]) == 4) {
                    asm_cvtsd2ss(a, i, i);
                }
            }
            asm_mov_reg_mem(a, REG_R11, REG_RSP, reg_scratch_off + nreg*8);
            asm_call_reg(a, REG_R11);
            asm_add_rsp(a, frame);
        } else {
            /* 32-bit: push args right-to-left, then call through register */
            /* Evaluate function pointer first, save on stack */
            codegen_expr(cg, n->fp_call.func_expr);
            asm_push_reg(a, REG_EAX);          /* save fn ptr */
            /* Push arguments right-to-left */
            for (int i = argc-1; i >= 0; i--) {
                codegen_expr(cg, n->fp_call.args[i]);
                asm_push_reg(a, REG_EAX);
            }
            /* Reload fn ptr and call */
            int fn_stack_off = argc * 4;       /* fn ptr is above the args */
            /* mov eax, [esp + fn_stack_off] */
            asm_emit1(a, 0x8B); asm_emit1(a, 0x44);
            asm_emit1(a, 0x24); asm_emit1(a, (uint8_t)fn_stack_off);
            asm_call_reg(a, REG_EAX);
            /* Callee is cdecl: caller cleans args + saved fn ptr */
            if (argc > 0) asm_add_rsp(a, argc * 4 + 4);
            else          asm_add_rsp(a, 4);
        }
        break;
    }

    case AST_BLOCK:
        /* Block used as expression (e.g. compound literal) — evaluate stmts, last val in RAX */
        for (int i=0;i<n->block.count;i++) {
            if (i==n->block.count-1)
                codegen_expr(cg,n->block.stmts[i]);
            else
                codegen_stmt(cg,n->block.stmts[i]);
        }
        break;

    default:
        printf("codegen_expr: unhandled node kind %d\n",n->kind);
        asm_mov_reg_imm(a,REG_RAX,0);
    }
}

/* =========================================================================
 * Inline assembly (GCC extended asm) — x86 / x86-64 codegen
 * =========================================================================
 * A genuinely working, but deliberately scoped, inline-assembler: supports
 * %N-numbered GCC constraint operands (register class 'r', the x86
 * single-letter registers a/b/c/d/S/D, memory class 'm', digit-tied
 * operands), raw physical register references (%%eax etc.), local numeric
 * jump labels (1:/1f/1b), '#'-to-end-of-line GAS comments, and a fixed set
 * of common mnemonics (data movement, arithmetic, atomics, flags,
 * CPUID/RDTSC, conditional jumps, the .byte raw-emission directive).
 * Unrecognized mnemonics/operand shapes are silently skipped rather than
 * mis-encoded — this is a practical subset, not a general assembler. */
#define ASM_MAX_OPS 16

typedef struct {
    int      is_output;
    int      is_readwrite;  /* '+' constraint prefix */
    int      is_memory;     /* constraint class 'm' */
    int      tied_to;       /* -1, or index of the operand this shares a register with (digit constraint) */
    char     reg_letter;    /* 0, or 'a','b','c','d','S','D' */
    ASTNode *expr;
    Reg      reg;           /* assigned register (holds value, or address if is_memory) */
    int      width;         /* 1/2/4/8 — byte width of the C operand's type */
} AsmOpX86;

typedef struct {
    int kind;         /* 0=unrecognized, 1=operand ref (%N), 2=raw register (%%name), 3=immediate, 4=local label ref (Nf/Nb) */
    int op_index;     /* kind==1 */
    int reg_id;       /* kind==2 */
    int reg_width;    /* kind==2: byte width implied by the register name itself (rax=8,eax=4,ax=2,al=1) */
    int is_deref;     /* wrapped in (...) -> memory access */
    long long disp;   /* displacement before '(' , e.g. "-8(%%ebp)" */
    long long imm;    /* kind==3 */
    int label_num;    /* kind==4 */
    int label_dir;    /* +1 = forward ('f'), -1 = backward ('b') */
} AsmTokX86;

/* Byte width implied by a raw x86 register NAME's own spelling, independent
 * of the assembler's target mode — "eax" is always 4 bytes wide, "rax"
 * always 8, even inside a 64-bit build. */
static int x86_asm_regname_width(const char *name) {
    int len=(int)strlen(name);
    if (len==0) return 4;
    if (name[0]=='r') return 8;
    if (name[0]=='e') return 4;
    if (len==2 && (name[1]=='l'||name[1]=='h')) return 1; /* al,bl,cl,dl,ah,bh,ch,dh */
    if (len==2) return 2; /* ax,bx,cx,dx,si,di,bp,sp */
    return 4;
}

static int x86_asm_reg_letter_id(char c) {
    switch (c) {
    case 'a': return REG_RAX; case 'b': return REG_RBX; case 'c': return REG_RCX;
    case 'd': return REG_RDX; case 'S': return REG_RSI; case 'D': return REG_RDI;
    default:  return -1;
    }
}

static int x86_asm_regname_to_id(const char *name) {
    static const struct { const char *n; int r; } tbl[] = {
        {"rax",REG_RAX},{"eax",REG_RAX},{"ax",REG_RAX},{"al",REG_RAX},
        {"rbx",REG_RBX},{"ebx",REG_RBX},{"bx",REG_RBX},{"bl",REG_RBX},
        {"rcx",REG_RCX},{"ecx",REG_RCX},{"cx",REG_RCX},{"cl",REG_RCX},
        {"rdx",REG_RDX},{"edx",REG_RDX},{"dx",REG_RDX},{"dl",REG_RDX},
        {"rsi",REG_RSI},{"esi",REG_RSI},{"si",REG_RSI},
        {"rdi",REG_RDI},{"edi",REG_RDI},{"di",REG_RDI},
        {"rbp",REG_RBP},{"ebp",REG_RBP},
        {"rsp",REG_RSP},{"esp",REG_RSP},
        {"r8",REG_R8},{"r9",REG_R9},{"r10",REG_R10},{"r11",REG_R11},
        {"r12",REG_R12},{"r13",REG_R13},{"r14",REG_R14},{"r15",REG_R15},
        {NULL,-1}
    };
    for (int i=0; tbl[i].n; i++) if (strcmp(tbl[i].n,name)==0) return tbl[i].r;
    return -1;
}

/* Byte width of an asm operand's C type; defaults to native register width
 * (8 in 64-bit mode, 4 otherwise) if unresolvable — matters for choosing
 * the right load/store instruction (e.g. "int" stays 4 bytes even when
 * cg->is_64bit, unlike pointers/long). */
static int asm_x86_operand_width(CodeGen *cg, ASTNode *expr) {
    int native = cg->is_64bit ? 8 : 4;
    if (!expr) return native;
    if (expr->kind == AST_VAR) {
        Symbol *s = symtable_lookup(cg->sym, expr->var.name);
        if (s && s->type) {
            if (s->type->pointer_depth > 0) return native;
            int sz = sizeof_type_sym(s->type, cg->is_64bit, cg->sym);
            if (sz==1||sz==2||sz==4||sz==8) return sz;
        }
    } else if (expr->kind == AST_DEREF && expr->deref.operand && expr->deref.operand->kind==AST_VAR) {
        Symbol *s = symtable_lookup(cg->sym, expr->deref.operand->var.name);
        if (s && s->type && s->type->pointer_depth >= 1) {
            if (s->type->pointer_depth >= 2) return native;
            TypeInfo tmp; memset(&tmp,0,sizeof tmp);
            tmp.base=s->type->base; tmp.pointer_depth=0; tmp.array_size=-1;
            tmp.is_unsigned=s->type->is_unsigned;
            int sz = sizeof_type_sym(&tmp, cg->is_64bit, cg->sym);
            if (sz==1||sz==2||sz==4||sz==8) return sz;
        }
    }
    return native;
}

/* Evaluate expr's VALUE into dst (input operands / read-write initial value). */
static void asm_x86_load_value_into(CodeGen *cg, ASTNode *expr, Reg dst) {
    Assembler *a = cg->asm_;
    codegen_expr(cg, expr);
    if (dst != REG_RAX) asm_mov_reg_reg(a, dst, REG_RAX);
}
/* Evaluate the ADDRESS a memory ('m') operand refers to into dst: for
 * "*ptr" that's ptr's own VALUE; otherwise it's expr's lvalue address. */
static void asm_x86_load_address_into(CodeGen *cg, ASTNode *expr, Reg dst) {
    Assembler *a = cg->asm_;
    if (expr && expr->kind == AST_DEREF) codegen_expr(cg, expr->deref.operand);
    else codegen_lvalue(cg, expr);
    if (dst != REG_RAX) asm_mov_reg_reg(a, dst, REG_RAX);
}
/* Classify one raw template operand token (whitespace already trimmed). */
static int asm_x86_classify_token(const char *tok_in, AsmTokX86 *out) {
    memset(out,0,sizeof *out);
    char tok[80]; snprintf(tok,sizeof tok,"%s",tok_in);
    char *p = tok;
    /* Declared at FUNCTION scope, not inside the "if (paren)" block below --
     * `p` is repointed at this buffer inside that block (p = inner) but
     * then read well past the block's own closing brace (every check
     * from here down to the function's end). A block-scoped `inner`
     * there was a real stack-use-after-scope bug (found via an
     * AddressSanitizer build while fuzzing this compiler against real,
     * legitimate large source files -- SDL3's own inline-asm-using code,
     * pulled in through SQW's single-TU build, was what actually
     * triggered it): `p` kept pointing at stack space whose lifetime had
     * already ended, undefined behavior that happened to usually still
     * "work" on this platform/stack-layout, exactly the kind of bug that
     * silently reads garbage (or crashes) only on some other platform,
     * optimization level, or stack layout. */
    char inner[64];
    char *paren = strchr(p,'(');
    if (paren) {
        long long disp = 0;
        if (paren != p) { char dbuf[32]; int dl=(int)(paren-p); if(dl>31)dl=31; memcpy(dbuf,p,dl); dbuf[dl]=0; disp = strtoll(dbuf,NULL,0); }
        char *close = strchr(paren,')');
        int len = close ? (int)(close-paren-1) : (int)strlen(paren+1);
        if (len>63) len=63;
        memcpy(inner,paren+1,len); inner[len]=0;
        out->is_deref = 1; out->disp = disp;
        p = inner;
    }
    if (p[0]=='%' && p[1]=='%') {
        int rid = x86_asm_regname_to_id(p+2);
        if (rid>=0) { out->kind=2; out->reg_id=rid; out->reg_width=x86_asm_regname_width(p+2); return 1; }
        return 0;
    }
    if (p[0]=='%' && isdigit((unsigned char)p[1])) { out->kind=1; out->op_index=atoi(p+1); return 1; }
    if (p[0]=='$') { out->kind=3; out->imm=strtoll(p+1,NULL,0); return 1; }
    if (isdigit((unsigned char)p[0]) && (p[1]=='f'||p[1]=='b') && p[2]=='\0') {
        out->kind=4; out->label_num=p[0]-'0'; out->label_dir=(p[1]=='f')?1:-1; return 1;
    }
    { char *endp; long long v = strtoll(p,&endp,0); if (endp!=p && *endp=='\0') { out->kind=3; out->imm=v; return 1; } }
    return 0;
}

static int asm_x86_resolve_operand(AsmOpX86 *ops, int n_ops, AsmTokX86 *tok, Reg *out_reg, int *out_is_mem, long long *out_disp, int *out_width) {
    *out_is_mem = tok->is_deref; *out_disp = tok->disp; *out_width = 0;
    if (tok->kind==2) { *out_reg=(Reg)tok->reg_id; *out_width=tok->reg_width; return 1; }
    if (tok->kind==1) {
        if (tok->op_index<0||tok->op_index>=n_ops) return 0;
        *out_reg = ops[tok->op_index].reg;
        *out_width = ops[tok->op_index].width;
        if (ops[tok->op_index].is_memory) *out_is_mem = 1;
        return 1;
    }
    return 0;
}

/* Encode one parsed instruction (mnemonic + up to 4 raw tokens) using the
 * operands' assigned registers. AT&T operand order: tok[0]=src, tok[1]=dst. */
static void asm_x86_encode_insn(CodeGen *cg, const char *mnemonic_in, AsmTokX86 *toks, int ntok,
                                 AsmOpX86 *ops, int n_ops, int local_labels[10]) {
    Assembler *a = cg->asm_;
    char mnem[16]; snprintf(mnem,sizeof mnem,"%s",mnemonic_in);
    for (char *q=mnem; *q; q++) *q = (char)tolower((unsigned char)*q);
    int explicit_width = 0; /* from a GAS size suffix (b/w/l/q) — authoritative when present */
    {
        static const char *bases[] = {"mov","xchg","cmpxchg","add","sub","and","or","xor","not","neg","inc","dec","cmp","test","push","pop",NULL};
        int len=(int)strlen(mnem);
        if (len>=2) {
            char last=mnem[len-1];
            if (last=='b'||last=='w'||last=='l'||last=='q') {
                char base[16]; snprintf(base,sizeof base,"%.*s",len-1,mnem);
                for (int bi=0; bases[bi]; bi++) if (strcmp(base,bases[bi])==0) {
                    strcpy(mnem,base);
                    explicit_width = (last=='b')?1:(last=='w')?2:(last=='l')?4:8;
                    break;
                }
            }
        }
    }

    Reg r0=REG_RAX, r1=REG_RAX; int mem0=0,mem1=0; long long disp0=0,disp1=0; long long imm0=0; int is_imm0=0;
    int w0=0, w1=0;
    if (ntok>=1) { if (toks[0].kind==3) { imm0=toks[0].imm; is_imm0=1; } else asm_x86_resolve_operand(ops,n_ops,&toks[0],&r0,&mem0,&disp0,&w0); }
    if (ntok>=2) { if (toks[1].kind==3) { imm0=toks[1].imm; is_imm0=1; } asm_x86_resolve_operand(ops,n_ops,&toks[1],&r1,&mem1,&disp1,&w1); }
    /* Width used for THIS instruction's encoding: the GAS suffix if given,
     * else whichever operand carries a known width, else native. */
    int width = explicit_width ? explicit_width : (w1 ? w1 : (w0 ? w0 : (cg->is_64bit?8:4)));

    if (strcmp(mnem,"mov")==0 && ntok==2) {
        if (is_imm0) {
            if (mem1) {
                /* Stage the immediate in a scratch register OTHER than r1 —
                 * r1 holds the destination ADDRESS for a memory operand, and
                 * since register allocation can (and often does) assign it
                 * RAX, blindly staging through RAX here would clobber the
                 * address before it's used, storing the value at whatever
                 * address the clobbered RAX now holds instead. */
                Reg tmp = (r1 != REG_RAX) ? REG_RAX : REG_RCX;
                asm_mov_reg_imm(a,tmp,imm0);
                if(width>=8 && cg->is_64bit) asm_mov_mem_reg(a,r1,(int)disp1,tmp); else asm_mov_mem32_reg(a,r1,(int)disp1,tmp);
            }
            else asm_mov_reg_imm(a,r1,imm0);
        } else if (mem0 && !mem1) {
            if (width>=8 && cg->is_64bit) asm_mov_reg_mem(a,r1,r0,(int)disp0); else asm_mov_reg32_mem(a,r1,r0,(int)disp0);
        } else if (!mem0 && mem1) {
            if (width>=8 && cg->is_64bit) asm_mov_mem_reg(a,r1,(int)disp1,r0); else asm_mov_mem32_reg(a,r1,(int)disp1,r0);
        } else {
            asm_mov_reg_reg(a,r1,r0);
        }
    }
    else if (strcmp(mnem,"xchg")==0 && ntok==2) {
        if (mem1) asm_xchg_reg_mem_w(a,r0,r1,(int)disp1,width);
        else if (mem0) asm_xchg_reg_mem_w(a,r1,r0,(int)disp0,width);
        else asm_xchg_reg_reg_w(a,r0,r1,width);
    }
    else if (strcmp(mnem,"cmpxchg")==0 && ntok==2) {
        if (mem1) asm_cmpxchg_mem_reg_w(a,r1,(int)disp1,r0,width);
        else if (mem0) asm_cmpxchg_mem_reg_w(a,r0,(int)disp0,r1,width);
        else asm_cmp_reg_reg(a,r1,r0);
    }
    else if ((strcmp(mnem,"add")==0||strcmp(mnem,"sub")==0||strcmp(mnem,"and")==0||strcmp(mnem,"or")==0||
              strcmp(mnem,"xor")==0||strcmp(mnem,"cmp")==0||strcmp(mnem,"test")==0) && ntok==2) {
        void (*fn)(Assembler*,Reg,Reg) = NULL; int op_ext=-1;
        if (strcmp(mnem,"add")==0){fn=asm_add_reg_reg;op_ext=0;}
        else if (strcmp(mnem,"sub")==0){fn=asm_sub_reg_reg;op_ext=5;}
        else if (strcmp(mnem,"and")==0){fn=asm_and_reg_reg;op_ext=4;}
        else if (strcmp(mnem,"or")==0){fn=asm_or_reg_reg;op_ext=1;}
        else if (strcmp(mnem,"xor")==0){fn=asm_xor_reg_reg;op_ext=6;}
        else if (strcmp(mnem,"cmp")==0){fn=asm_cmp_reg_reg;op_ext=7;}
        else if (strcmp(mnem,"test")==0){fn=asm_test_reg_reg;}
        if (is_imm0 && !mem1 && op_ext>=0) asm_alu_reg_imm_w(a,op_ext,r1,imm0,width);
        else if (!mem0 && !mem1 && fn) fn(a,r1,r0);
    }
    else if (strcmp(mnem,"lock")==0) asm_lock_prefix(a);
    else if (strcmp(mnem,"nop")==0) asm_nop(a);
    else if (strcmp(mnem,"pause")==0) asm_pause(a);
    else if (strcmp(mnem,"hlt")==0) asm_hlt(a);
    else if (strcmp(mnem,"cpuid")==0) asm_cpuid(a);
    else if (strcmp(mnem,"rdtsc")==0) asm_rdtsc(a);
    else if (strcmp(mnem,"pushf")==0||strcmp(mnem,"pushfd")==0||strcmp(mnem,"pushfq")==0) asm_pushf(a);
    else if (strcmp(mnem,"popf")==0||strcmp(mnem,"popfd")==0||strcmp(mnem,"popfq")==0) asm_popf(a);
    else if (strcmp(mnem,"push")==0 && ntok==1) { if (is_imm0) { asm_mov_reg_imm(a,REG_RAX,imm0); asm_push_reg(a,REG_RAX); } else asm_push_reg(a,r0); }
    else if (strcmp(mnem,"pop")==0 && ntok==1) asm_pop_reg(a,r0);
    else if (strcmp(mnem,"not")==0 && ntok==1) asm_not_reg(a,r0);
    else if (strcmp(mnem,"neg")==0 && ntok==1) asm_neg_reg(a,r0);
    else if (strcmp(mnem,"inc")==0 && ntok==1) asm_inc_reg_w(a,r0,width);
    else if (strcmp(mnem,"dec")==0 && ntok==1) asm_dec_reg_w(a,r0,width);
    else if (strcmp(mnem,"int")==0 && ntok==1) asm_int_imm8(a,(uint8_t)imm0);
    else if (ntok==1 && toks[0].kind==4 &&
             (strcmp(mnem,"jmp")==0||strcmp(mnem,"jz")==0||strcmp(mnem,"je")==0||strcmp(mnem,"jnz")==0||strcmp(mnem,"jne")==0)) {
        int ln = toks[0].label_num;
        if (local_labels[ln] < 0) local_labels[ln] = asm_new_label(a,"asmlbl");
        if (strcmp(mnem,"jmp")==0) asm_jmp_label(a, local_labels[ln]);
        else if (strcmp(mnem,"jz")==0||strcmp(mnem,"je")==0) asm_jcc_label(a, CC_E, local_labels[ln]);
        else asm_jcc_label(a, CC_NE, local_labels[ln]);
    }
    /* unrecognized mnemonic/operand shape: skipped (best-effort subset) */
}

static void codegen_asm_x86(CodeGen *cg, ASTNode *n) {
    Assembler *a = cg->asm_;
    AsmOpX86 ops[ASM_MAX_OPS]; int n_ops=0;
    int n_outputs = n->asm_stmt.n_outputs;

    for (int i=0;i<n->asm_stmt.n_outputs && n_ops<ASM_MAX_OPS;i++) {
        const char *c = n->asm_stmt.outputs[i].constraint;
        AsmOpX86 *op = &ops[n_ops++];
        memset(op,0,sizeof *op);
        op->is_output=1; op->tied_to=-1; op->expr=n->asm_stmt.outputs[i].expr;
        int ci=0;
        while (c[ci]=='='||c[ci]=='+'||c[ci]=='&') { if(c[ci]=='+') op->is_readwrite=1; ci++; }
        char cls[16]; int cl=0; while (c[ci] && c[ci]!=',' && cl<15) cls[cl++]=c[ci++]; cls[cl]=0;
        if (cl>0) {
            if (isdigit((unsigned char)cls[0])) op->tied_to = atoi(cls);
            else if (cls[0]=='m') op->is_memory=1;
            else if (cl==1 && x86_asm_reg_letter_id(cls[0])>=0) op->reg_letter=cls[0];
        }
        op->width = asm_x86_operand_width(cg, op->expr);
    }
    for (int i=0;i<n->asm_stmt.n_inputs && n_ops<ASM_MAX_OPS;i++) {
        const char *c = n->asm_stmt.inputs[i].constraint;
        AsmOpX86 *op = &ops[n_ops++];
        memset(op,0,sizeof *op);
        op->is_output=0; op->tied_to=-1; op->expr=n->asm_stmt.inputs[i].expr;
        int ci=0;
        while (c[ci]=='&') ci++;
        char cls[16]; int cl=0; while (c[ci] && c[ci]!=',' && cl<15) cls[cl++]=c[ci++]; cls[cl]=0;
        if (cl>0) {
            if (isdigit((unsigned char)cls[0])) op->tied_to = atoi(cls);
            else if (cls[0]=='m') op->is_memory=1;
            else if (cl==1 && x86_asm_reg_letter_id(cls[0])>=0) op->reg_letter=cls[0];
        }
        op->width = asm_x86_operand_width(cg, op->expr);
    }

    /* Evaluate operand expressions BEFORE any register is assigned — stash
     * each on the stack, since evaluating operand i+1 would otherwise
     * clobber whatever scratch register operand i's value ended up in. */
    int has_slot[ASM_MAX_OPS];
    for (int i=0;i<n_ops;i++) {
        /* Register-class pure outputs (no "+", not tied) need nothing
         * preloaded — the template computes their value from scratch. But
         * a MEMORY-class operand's register always holds an ADDRESS, not
         * "the output value", and that address must be computed either
         * way — there's no template instruction that could produce it, so
         * this is unconditional regardless of input/output direction. */
        int need_init = ops[i].is_memory || !(ops[i].is_output && !ops[i].is_readwrite && ops[i].tied_to<0);
        has_slot[i]=need_init;
        if (!need_init) continue;
        if (ops[i].is_memory) asm_x86_load_address_into(cg, ops[i].expr, REG_RAX);
        else asm_x86_load_value_into(cg, ops[i].expr, REG_RAX);
        asm_push_reg(a, REG_RAX);
    }

    /* Register allocation: single-letter constraints claim their fixed
     * register first, then generic 'r'/'m' operands take the next free slot
     * from the scratch pool, then tied (digit-constraint) operands copy
     * whichever register their target already got. Registers named in the
     * clobber list ("%eax", "ecx", etc — the leading '%' some code writes
     * is optional) are excluded from the pool: the template uses them
     * directly via raw %%reg references for its own scratch work (e.g.
     * CPU_haveCPUID's pushfq/popq %%rax dance), so an operand assigned one
     * of those would get silently overwritten mid-template. */
    int used[16]; for (int i=0;i<16;i++) used[i]=0;
    for (int i=0;i<n->asm_stmt.n_clobbers;i++) {
        const char *cl = n->asm_stmt.clobbers[i];
        if (cl[0]=='%') cl++;
        int r = x86_asm_regname_to_id(cl);
        if (r>=0) used[r]=1;
    }
    for (int i=0;i<n_ops;i++) if (ops[i].reg_letter) { int r=x86_asm_reg_letter_id(ops[i].reg_letter); ops[i].reg=(Reg)r; used[r]=1; }
    static const Reg pool64[] = {REG_RAX,REG_RCX,REG_RDX,REG_RBX,REG_RSI,REG_RDI,REG_R8,REG_R9,REG_R10,REG_R11,REG_R12,REG_R13,REG_R14,REG_R15};
    static const Reg pool32[] = {REG_RAX,REG_RCX,REG_RDX,REG_RBX,REG_RSI,REG_RDI};
    const Reg *pool = cg->is_64bit ? pool64 : pool32;
    int poolsz = cg->is_64bit ? 14 : 6;
    int pi=0;
    for (int i=0;i<n_ops;i++) {
        if (ops[i].reg_letter || ops[i].tied_to>=0) continue;
        while (pi<poolsz && used[pool[pi]]) pi++;
        if (pi<poolsz) { ops[i].reg = pool[pi]; used[pool[pi]]=1; pi++; }
        else ops[i].reg = REG_RAX;
    }
    for (int pass=0; pass<3; pass++)
        for (int i=0;i<n_ops;i++)
            if (ops[i].tied_to>=0 && ops[i].tied_to<n_ops) ops[i].reg = ops[ops[i].tied_to].reg;

    if (getenv("SQUASH_ASM_DEBUG")) {
        for (int i=0;i<n_ops;i++)
            fprintf(stderr, "[asmdbg] op %d: is_output=%d letter=%c tied=%d is_mem=%d reg=%d width=%d\n",
                    i, ops[i].is_output, ops[i].reg_letter?ops[i].reg_letter:'-', ops[i].tied_to, ops[i].is_memory, (int)ops[i].reg, ops[i].width);
    }

    /* Pop stashed values directly into their final registers, in reverse
     * push order. Must pop straight into ops[i].reg (not stage through a
     * fixed temp like RAX first) — staging through one shared register
     * would clobber an earlier pop in this same loop whenever THAT
     * operand's final register also happened to be the staging register
     * (e.g. a tied "0"(1) input sharing output 0's RAX, popped first,
     * then immediately overwritten by the next operand's own pop-through-
     * RAX before it could be moved out). */
    for (int i=n_ops-1;i>=0;i--) {
        if (!has_slot[i]) continue;
        asm_pop_reg(a, ops[i].reg);
    }

    /* Parse and encode the template, statement by statement. */
    int local_labels[10]; for (int i=0;i<10;i++) local_labels[i]=-1;
    char buf[4096]; snprintf(buf,sizeof buf,"%s",n->asm_stmt.template_str);
    { /* strip '#'-to-end-of-line GAS comments */
        char *ln = buf;
        while (*ln) {
            char *h = strchr(ln,'#'); char *nl = strchr(ln,'\n');
            if (h && (!nl || h<nl)) { char *e = nl?nl:(ln+strlen(ln)); memmove(h,e,strlen(e)+1); }
            if (!nl) break;
            ln = nl+1;
        }
    }
    {
        char *p = buf;
        while (*p) {
            while (*p==';'||*p=='\n'||*p=='\t'||*p==' '||*p=='\r') p++;
            if (!*p) break;
            char *start=p;
            while (*p && *p!=';' && *p!='\n') p++;
            int len=(int)(p-start); if (len>511) len=511;
            char stmt[512]; memcpy(stmt,start,len); stmt[len]=0;
            int sl=(int)strlen(stmt);
            while (sl>0 && (stmt[sl-1]==' '||stmt[sl-1]=='\t'||stmt[sl-1]=='\r')) stmt[--sl]=0;
            char *s2=stmt; while (*s2==' '||*s2=='\t') s2++;
            if (*s2) {
                char *colon = strchr(s2,':');
                int label_ok = 0;
                if (colon && colon[1]=='\0' && colon!=s2) {
                    label_ok = 1;
                    for (char *d=s2; d<colon; d++) if (!isdigit((unsigned char)*d)) { label_ok=0; break; }
                }
                if (label_ok) {
                    int ln2 = s2[0]-'0';
                    if (local_labels[ln2]<0) local_labels[ln2]=asm_new_label(a,"asmlbl");
                    asm_def_label(a, local_labels[ln2]);
                } else if (strncmp(s2,".byte",5)==0) {
                    char *rest = s2+5;
                    char *tok = strtok(rest,",");
                    while (tok) {
                        while (*tok==' '||*tok=='\t') tok++;
                        long long v = strtoll(tok,NULL,0);
                        asm_emit1(a,(uint8_t)v);
                        tok = strtok(NULL,",");
                    }
                } else {
                    char mnem[16]; int mi=0; char *s3=s2;
                    while (*s3 && *s3!=' ' && *s3!='\t' && mi<15) mnem[mi++]=*s3++;
                    mnem[mi]=0;
                    while (*s3==' '||*s3=='\t') s3++;
                    AsmTokX86 toks[4]; int ntok=0;
                    if (*s3) {
                        char *tok2 = strtok(s3,",");
                        while (tok2 && ntok<4) {
                            while (*tok2==' '||*tok2=='\t') tok2++;
                            int e=(int)strlen(tok2); while (e>0 && (tok2[e-1]==' '||tok2[e-1]=='\t')) tok2[--e]=0;
                            asm_x86_classify_token(tok2,&toks[ntok]);
                            ntok++;
                            tok2 = strtok(NULL,",");
                        }
                    }
                    asm_x86_encode_insn(cg, mnem, toks, ntok, ops, n_ops, local_labels);
                }
            }
        }
    }

    /* Write outputs back to their C lvalues. Memory-class ("m") outputs are
     * skipped here — their register holds the ADDRESS the template wrote
     * through directly (e.g. "movl $1,%0" with "=m"), not a value that
     * still needs storing.
     *
     * Push every output's value onto the stack FIRST, before computing any
     * lvalue address — asm_x86_store_value's own internal scratch register
     * (RCX) can otherwise clobber a DIFFERENT, not-yet-written-back output
     * still sitting in its own assigned register (e.g. cpuid()'s "=c"(c)
     * output lives in RCX; writing back an earlier "=a"/"=S" output first
     * would stomp RCX via that same internal scratch before c's own turn
     * came). Once everything is safely on the stack, address computation
     * can use any register freely. */
    for (int i=0;i<n_outputs;i++) if (!ops[i].is_memory) asm_push_reg(a, ops[i].reg);
    for (int i=n_outputs-1;i>=0;i--) {
        if (ops[i].is_memory) continue;
        codegen_lvalue(cg, ops[i].expr);          /* RAX = &expr (free to clobber anything now) */
        asm_mov_reg_reg(a, REG_RCX, REG_RAX);
        asm_pop_reg(a, REG_RAX);                  /* RAX = this output's real value, off the stack */
        if (ops[i].width==1) asm_mov_mem8_reg(a, REG_RCX, 0, REG_RAX);
        else if (ops[i].width>=8 && cg->is_64bit) asm_mov_mem_reg(a, REG_RCX, 0, REG_RAX);
        else asm_mov_mem32_reg(a, REG_RCX, 0, REG_RAX);
    }
}

/* =========================================================================
 * codegen_stmt
 * ========================================================================= */
void codegen_stmt(CodeGen *cg, ASTNode *n) {
    Assembler *a=cg->asm_;
    if (!n) return;
    switch (n->kind) {
    case AST_BLOCK: {
        /* Check if this is a flat declarator list (all children are VAR_DECL).
         * Such blocks come from multi-declarator statements like "int a=1, b=2;"
         * They must NOT push a new scope — vars belong to the current scope. */
        int all_var_decl = (n->block.count > 0);
        for (int i=0; i<n->block.count && all_var_decl; i++)
            if (!n->block.stmts[i] || n->block.stmts[i]->kind != AST_VAR_DECL)
                all_var_decl = 0;
        if (all_var_decl) {
            for (int i=0;i<n->block.count;i++) { codegen_stmt(cg,n->block.stmts[i]); }
        } else {
            symtable_push_scope(cg->sym);
            for (int i=0;i<n->block.count;i++) { codegen_stmt(cg,n->block.stmts[i]); }
            symtable_pop_scope(cg->sym);
        }
        break;
    }

    case AST_EXPR_STMT:
        codegen_expr(cg, n->expr_stmt.expr);
        /* 32-bit: float assignment and call expression-statements leave their
         * result in ST0 (via the fstp+fld reload pattern used to support chained
         * assignments).  In statement context the result is never consumed, so
         * the x87 FPU stack accumulates leaked entries.  After 8 such leaks the
         * stack overflows and subsequent float operations produce NaN.
         * Emit "fstp st(0)" (DD D8) to discard the stale ST0 and keep the stack
         * balanced.  Only applies to node kinds that actually deposit in ST0. */
        if (!cg->is_64bit && codegen_is_float_expr(cg, n->expr_stmt.expr)) {
            ASTNode *_fe = n->expr_stmt.expr;
            if (_fe->kind == AST_ASSIGN || _fe->kind == AST_CALL  ||
                _fe->kind == AST_FLOAT  || _fe->kind == AST_BINARY ||
                _fe->kind == AST_UNARY  || _fe->kind == AST_CAST)
                asm_emit2(a, 0xDD, 0xD8); /* fstp st(0) — discard unused float result */
        }
        break;

    case AST_VAR_DECL: {
        /* Stamp array_size onto the TypeInfo so symtable_define_var
           allocates the correct number of bytes for the whole array. */
        int arr=n->var_decl.array_size;
        if (n->var_decl.type && arr > 0)
            n->var_decl.type->array_size = arr;

        /* Static local variables live in the data section, not on the stack.
         * We allocate space in the data segment and access via RIP-relative
         * (64-bit) or absolute address (32-bit). */
        int is_static = (n->var_decl.storage &&
                         strcmp(n->var_decl.storage,"static")==0);
        if (is_static) {
            /* typeinfo_size() has no SymTable access and silently returns a
             * hardcoded 4 for ANY struct/union base type ("default for
             * structs etc" — ast.c) — correct for scalar types, but wildly
             * wrong for a function-local "static SomeStruct var;" whose real
             * size is larger. This under-reserves the wdata pool slot, and
             * the NEXT static local declared in the same function then gets
             * an offset that lands inside THIS one's real (but unreserved)
             * memory footprint — confirmed via a minimal repro (a struct
             * containing an array-of-struct field, immediately followed by
             * a second static local, silently overlapping at +8 instead of
             * +sizeof(struct)). sizeof_type_sym() resolves struct/union
             * sizes properly via the symbol table and is used everywhere
             * else in this file for exactly this reason. */
            /* sizeof_type_sym() itself already multiplies by array_size when
             * it's set (>0) — and array_size was just stamped onto this
             * exact TypeInfo a few lines above, for the whole function's
             * "arr > 0" case (not just this static-local branch). Query it
             * with array_size temporarily cleared so "elem_sz" really means
             * one element, not the whole array — otherwise "total_sz =
             * elem_sz * arr" below double-multiplies (confirmed via a
             * minimal repro: a 5-element static array reserved 25 bytes of
             * wdata instead of 5, and — found while adding real static-
             * local-initializer support just below — packed each real
             * initializer value 5 bytes apart instead of 1, corrupting
             * every element but the first). */
            int elem_sz;
            if (n->var_decl.type) {
                int saved_arr = n->var_decl.type->array_size;
                n->var_decl.type->array_size = 0;
                elem_sz = sizeof_type_sym(n->var_decl.type, cg->is_64bit, cg->sym);
                n->var_decl.type->array_size = saved_arr;
            } else {
                elem_sz = 4;
            }
            if (elem_sz < 1) elem_sz = 4;
            int total_sz = (arr > 0) ? elem_sz * arr : elem_sz;

            /* Unique mangled label */
            char sym_name[256];
            snprintf(sym_name, sizeof sym_name, "static_%s_%d",
                     n->var_decl.name, cg->string_count);

            /* Register in writable data pool (.data section, R/W). Static
             * vars must be writable — they store live state across calls.
             * If the initializer is a compile-time constant (the common
             * case for a lookup table, e.g. miniz.h's/stb_image.h's several
             * "static const T table[] = {...};" locals), bake the real
             * values in now — matching how real compilers already handle
             * this exact case, no runtime init code needed at all. Anything
             * fancier (non-constant initializer) falls back to the
             * pre-existing zero-fill behavior. */
            uint8_t *const_bytes = build_static_local_init_bytes(n->var_decl.init, elem_sz, arr);
            if (const_bytes) {
                intern_wdata_init(cg, sym_name, total_sz, const_bytes);
                free(const_bytes);
            } else {
                intern_wdata(cg, sym_name, total_sz);
            }

            /* Register in symbol table under the ORIGINAL name so user code
             * can continue referencing "count", "total", etc.                */
            TypeInfo *ti = n->var_decl.type ? n->var_decl.type : typeinfo_new("int");
            symtable_define_global(cg->sym, n->var_decl.name, ti, arr);
            Symbol *user_sym = symtable_lookup(cg->sym, n->var_decl.name);
            if (user_sym) {
                /* Store the rdata label in sym->dll (repurposed for static vars)
                 * so codegen_expr can find it while lookup by original name works. */
                free(user_sym->dll);
                user_sym->dll = my_strdup(sym_name);
            }

            /* Static variable storage is zero-initialised via calloc() in the
             * wdata pool — no inline init code is emitted.  C semantics require
             * static locals to be initialised only once (at program start), not
             * on every function call.  Emitting inline init code would reset the
             * variable on every invocation, which is wrong.
             * Non-zero initialisers: the wdata is pre-zeroed; a non-zero value
             * can be set by the programmer on first call with an if-not-set guard,
             * or we handle it as a one-time init (future work).               */
            (void)sym_name; /* used only for relocation symbol */
            break;
        }

        /* Resolve typedef aliases BEFORE defining the var, so the correct
           size is allocated. E.g. "myfloat" -> TypeInfo{base="double"} -> 8 bytes */
        TypeInfo *vdtype = n->var_decl.type;
        if (vdtype && vdtype->base && vdtype->pointer_depth==0) {
            Symbol *tds = symtable_lookup(cg->sym, vdtype->base);
            if (tds && tds->kind==SYM_TYPEDEF && tds->type)
                vdtype = tds->type;
        }
        /* If typedef resolves to float type, use the resolved type for size calc */
        TypeInfo *effective_type = (vdtype != n->var_decl.type) ? vdtype : n->var_decl.type;
        /* Temporarily patch the type so symtable_define_var gets right size.
         * Also preserve the array_size from the original declaration (e.g. DLLGroup g[64]
         * resolves to the anonymous struct type, but array_size must remain 64). */
        TypeInfo *saved_type = n->var_decl.type;
        int saved_arr_size = effective_type->array_size;
        if (arr > 0 && effective_type->array_size != arr)
            effective_type->array_size = arr;
        n->var_decl.type = effective_type;
        Symbol *sym=symtable_define_var(cg->sym,n->var_decl.name,n->var_decl.type);
        effective_type->array_size = saved_arr_size;  /* restore */
        n->var_decl.type = saved_type;  /* restore original */

        /* VLA: runtime-sized array — allocate via malloc(count * elem_size) */
        if (n->var_decl.vla_expr && arr == -2) {
            int elem_sz = typeinfo_size(effective_type, cg->is_64bit);
            if (elem_sz < 1) elem_sz = 1;
            /* Evaluate count expression → RAX */
            codegen_expr(cg, n->var_decl.vla_expr);
            if (elem_sz > 1) {
                /* imul rax, elem_sz (64-bit: REX.W + 0x69 /r imm32) */
                if (cg->is_64bit) {
                    asm_emit3(a,0x48,0x6B,0xC0); asm_emit1(a,(uint8_t)elem_sz);
                } else {
                    asm_emit2(a,0x6B,0xC0); asm_emit1(a,(uint8_t)elem_sz);
                }
            }
            /* Generate call to malloc with size in first arg register */
            { ASTNode *sz_arg = ast_number(0, n->line); /* placeholder */
              ASTNode *args_arr[1]; args_arr[0] = sz_arg;
              /* For the call, we need size in the arg register already.
               * Approach: manually put RAX in the right arg reg, then emit call.
               * Windows x64: RCX; SysV x64: RDI; 32-bit: push EAX */
              if (cg->is_64bit && !a->is_linux) {
                  /* mov rcx, rax */
                  asm_emit3(a,0x48,0x89,0xC1);
              } else if (cg->is_64bit) {
                  /* mov rdi, rax */
                  asm_emit3(a,0x48,0x89,0xC7);
              } else {
                  /* push eax */
                  asm_emit1(a,0x50);
              }
              /* Emit direct call to malloc */
              ASTNode *_mcall = ast_call("malloc", args_arr, 0, n->line);
              /* We emit the call with 0 real args because we already set up the reg */
              codegen_expr(cg, _mcall);
              if (!cg->is_64bit) {
                  /* add esp, 4 (clean up pushed arg) */
                  asm_emit3(a,0x83,0xC4,0x04);
              }
              ast_free(sz_arg);
            }
            /* RAX now holds the malloc'd pointer — store it as the local variable */
            asm_mov_mem_reg(a, REG_RBP, sym->offset, REG_RAX);
            break;
        }

        int is_float_var = typeinfo_is_float(effective_type);
        if (is_float_var) {
            /* Allocate 8-byte slot for double, 4-byte for float */
            int fsz = effective_typeinfo_size(cg, effective_type, cg->is_64bit);
            /* float/double ARRAY initialiser: "float verts[] = {1.0f, -2.0f, ...};"
             * Each element must go through the float path individually — falling
             * through to the generic AST_BLOCK-as-expression case below (which
             * evaluates all-but-the-last element via codegen_stmt, meant for
             * comma-expression side effects) can't handle bare literal/unary
             * nodes as statements. */
            if (n->var_decl.init && n->var_decl.init->kind==AST_BLOCK && arr>0) {
                for (int i=0;i<n->var_decl.init->block.count && i<arr; i++) {
                    ASTNode *elem = n->var_decl.init->block.stmts[i];
                    int disp = sym->offset + i*fsz;
                    if (elem && codegen_is_float_expr(cg, elem)) {
                        codegen_float_expr(cg, elem);
                        if (cg->is_64bit) {
                            if (fsz==4) { asm_cvtsd2ss(a,0,0); asm_movss_store(a,REG_RBP,disp,0); }
                            else asm_movsd_store(a,REG_RBP,disp,0);
                        } else { asm_fstp_mem64(a,REG_RBP,disp); }
                    } else if (elem) {
                        /* Bare integer literal in a float/double ARRAY
                         * initializer (e.g. "float data[8] = {0,1,...,7};",
                         * no ".0f" suffix — codegen_is_float_expr() above is
                         * false for these, since they're AST_NUMBER not
                         * AST_FLOAT). Must respect fsz here exactly like the
                         * float-literal branch just above: for a 4-byte
                         * float ARRAY, storing the int->double conversion
                         * result as a full 8-byte movsd (ignoring fsz)
                         * writes 8 bytes at a stride the loop only advances
                         * by 4 — every element but the last overlaps the
                         * next, and the LAST element's write runs 4 bytes
                         * past the array's own total allocation, corrupting
                         * whatever stack variable/slot happens to sit right
                         * after it. Confirmed via a minimal repro (a
                         * later-declared cl_kernel-handle-shaped pointer
                         * variable had its low 4 bytes silently overwritten
                         * by a preceding "float data[8] = {0,1,...,7};"),
                         * which was the real root cause of a genuine OpenCL
                         * clSetKernelArg crash — dispatching through a
                         * corrupted kernel handle, not a calling-convention
                         * or alignment bug as it first appeared. */
                        codegen_expr(cg, elem);
                        if (cg->is_64bit) {
                            asm_cvtsi2sd(a,0,REG_RAX);
                            if (fsz==4) { asm_cvtsd2ss(a,0,0); asm_movss_store(a,REG_RBP,disp,0); }
                            else asm_movsd_store(a,REG_RBP,disp,0);
                        }
                        else { asm_sub_rsp(a,4); asm_mov_mem_reg(a,REG_ESP,0,REG_EAX); asm_fild_mem32(a,REG_ESP,0); asm_fstp_mem64(a,REG_RBP,disp); asm_add_rsp(a,4); }
                    } else {
                        if (cg->is_64bit) {
                            asm_xorpd(a,0,0);
                            if (fsz==4) asm_movss_store(a,REG_RBP,disp,0);
                            else asm_movsd_store(a,REG_RBP,disp,0);
                        }
                        else { asm_fldz(a); asm_fstp_mem64(a,REG_RBP,disp); }
                    }
                }
                break;
            }
            /* sym->offset already set by symtable_define_var */
            if (n->var_decl.init && codegen_is_float_expr(cg, n->var_decl.init)) {
                codegen_float_expr(cg, n->var_decl.init);
                if (cg->is_64bit) {
                    if (fsz==4) { asm_cvtsd2ss(a,0,0); asm_movss_store(a,REG_RBP,sym->offset,0); }
                    else asm_movsd_store(a,REG_RBP,sym->offset,0);
                } else { asm_fstp_mem64(a,REG_RBP,sym->offset); }
            } else if (n->var_decl.init) {
                /* int->float init */
                codegen_expr(cg, n->var_decl.init);
                if (cg->is_64bit) { asm_cvtsi2sd(a,0,REG_RAX); asm_movsd_store(a,REG_RBP,sym->offset,0); }
                else { asm_sub_rsp(a,4); asm_mov_mem_reg(a,REG_ESP,0,REG_EAX); asm_fild_mem32(a,REG_ESP,0); asm_fstp_mem64(a,REG_RBP,sym->offset); asm_add_rsp(a,4); }
            } else {
                /* Zero-init */
                if (cg->is_64bit) { asm_xorpd(a,0,0); asm_movsd_store(a,REG_RBP,sym->offset,0); }
                else { asm_fldz(a); asm_fstp_mem64(a,REG_RBP,sym->offset); }
            }
            break;
        }
        if (n->var_decl.init) {
            if (n->var_decl.init->kind==AST_STRING && arr>0 && !n->var_decl.init->str.is_wide) {
                /* char arr[N] = "text"; — a fixed-size LOCAL char array
                 * initialized directly from a string literal (as opposed to
                 * a "char *p = \"text\";" pointer, which just needs the
                 * literal's address, already handled by the generic
                 * scalar-initializer fallback further below). Nothing above
                 * this point recognized this shape at all: init->kind is
                 * AST_STRING, not AST_BLOCK, so it fell through every check
                 * here into that generic fallback, which does
                 * "codegen_expr(cg, n->var_decl.init)" (the string literal's
                 * RIP-relative ADDRESS, in RAX) and then a "size-aware
                 * store" sized off typeinfo_size(sym->type,1) — for a char
                 * array that's 1 byte, so it stored only the low byte of
                 * the STRING'S ADDRESS into arr[0] and left every other
                 * byte (including the rest of the real string content and
                 * its NUL terminator) as whatever was already on the stack.
                 * Confirmed via a minimal repro: "char buf[8] = \"hi\";"
                 * produced buf[0]==(garbage address byte), buf[1..]
                 * untouched, instead of 'h','i',0,0,0,0,0,0 — found while
                 * self-hosting squash itself for -macos (diag.c's own
                 * "char sugbuf[160] = \"\";" was the actual trigger: an
                 * uninitialized buf[0] fed straight into printf's "%s"). *
                 * The string's bytes are a compile-time constant, so they're
                 * baked in directly as per-byte immediate stores (no runtime
                 * copy loop needed) — same "constant data known at codegen
                 * time" spirit as this file's static-local-initializer
                 * fast path. Bytes beyond the literal's length (including
                 * its own NUL terminator) are zero-filled up to `arr`,
                 * matching C's "remaining elements are zero-initialized"
                 * initializer-list rule; bytes beyond `arr` are dropped
                 * (matching C's "char a[2]=\"hi\"" no-implicit-NUL rule). */
                const char *sval = n->var_decl.init->str.value ? n->var_decl.init->str.value : "";
                int slen = (int)strlen(sval);
                for (int i=0; i<arr; i++) {
                    uint8_t byte = (i <= slen) ? (uint8_t)sval[i] : 0; /* i==slen: the NUL terminator */
                    asm_mov_reg_imm(a, REG_RAX, (long long)byte);
                    int disp = sym->offset + i;
                    if (disp >= -128 && disp <= 127) {
                        asm_emit3(a,0x88,0x45,(uint8_t)(int8_t)disp);
                    } else {
                        asm_emit2(a,0x88,0x85);
                        asm_emit4(a,(uint8_t)disp,(uint8_t)(disp>>8),(uint8_t)(disp>>16),(uint8_t)(disp>>24));
                    }
                }
            } else if (n->var_decl.init->kind==AST_BLOCK && arr>0) {
                /* Array-of-struct/union check FIRST: the parser flattens
                 * nested per-element "{...}" braces into one flat scalar
                 * list (same flattening the global array-of-structs branch
                 * and DEFINE_GUID comments elsewhere in this file describe),
                 * so for e.g. "Vertex verts[3] = {{0,0.5,1,0,0},{0.5,-0.5,
                 * 0,1,0},{-0.5,-0.5,0,0,1}};" (a real Vulkan/D3D11 triangle
                 * demo's vertex buffer) block.count is 15 (3 structs * 5
                 * float fields), not 3. Falling into the generic scalar-array
                 * path below — which assumes each list entry is one whole
                 * array-element value — took only the first `arr` (3) FLAT
                 * SCALARS (verts[0].x, .y, .r) as if each were one whole
                 * Vertex, evaluated each with the generic (non-float-aware)
                 * codegen_expr(), and stored the raw 8-byte result at a
                 * sizeof(Vertex)=20-byte stride. This corrupted nearly all
                 * real field data with no API error anywhere downstream —
                 * confirmed as the root cause of a real Vulkan demo silently
                 * rendering nothing (ground-truth GPU readback showed the
                 * vertex buffer itself already held garbage/zeroed floats
                 * before upload) even though every Vulkan call reported
                 * success. Fixed by detecting the struct/union element type
                 * (same is_struct_base + typedef-fallback check the global
                 * path and the arr<=0 local-struct path both already use)
                 * and expanding field-by-field via ordinary AST_MEMBER/
                 * AST_ASSIGN + codegen_stmt (which already gets per-field
                 * widths and float stores right), mirroring the working
                 * global array-of-structs expansion. */
                int handled_struct_array = 0;
                {
                    const char *base = effective_type ? effective_type->base : NULL;
                    int is_struct_base = base && effective_type->pointer_depth==0 &&
                        (strncmp(base,"struct ",7)==0 || strncmp(base,"union ",6)==0);
                    if (!is_struct_base && base && effective_type->pointer_depth==0) {
                        Symbol *btd = symtable_lookup(cg->sym, base);
                        if (btd && btd->kind==SYM_TYPEDEF && btd->type && btd->type->base &&
                            (strncmp(btd->type->base,"struct ",7)==0 || strncmp(btd->type->base,"union ",6)==0)) {
                            base = btd->type->base;
                            is_struct_base = 1;
                        }
                    }
                    Symbol *esym = is_struct_base ? symtable_lookup(cg->sym, base) : NULL;
                    ASTNode *esd = (esym && esym->struct_node) ? esym->struct_node : NULL;
                    if (esd && esd->struct_decl.nfields>0) {
                        handled_struct_array = 1;
                        int nf = esd->struct_decl.nfields;
                        int per_elem_flat = 0;
                        for (int fi=0; fi<nf; fi++) {
                            ASTNode *ff0 = esd->struct_decl.fields[fi];
                            per_elem_flat += (ff0 && ff0->kind==AST_FIELD && ff0->field.array_size>0) ? ff0->field.array_size : 1;
                        }
                        if (per_elem_flat < 1) per_elem_flat = nf;
                        int true_n = n->var_decl.init->block.count / per_elem_flat;
                        if (true_n > arr) true_n = arr;
                        int flat = 0;
                        for (int idx=0; idx<true_n && flat<n->var_decl.init->block.count; idx++) {
                            for (int fi=0; fi<nf && flat<n->var_decl.init->block.count; fi++) {
                                ASTNode *ff = esd->struct_decl.fields[fi];
                                if (!ff || ff->kind!=AST_FIELD || !ff->field.name) { flat++; continue; }
                                if (ff->field.array_size > 0) {
                                    for (int ei=0; ei<ff->field.array_size && flat<n->var_decl.init->block.count; ei++, flat++) {
                                        ASTNode *sub = n->var_decl.init->block.stmts[flat];
                                        if (!sub || sub->kind==AST_BLOCK) continue;
                                        ASTNode *lhs = ast_index(
                                            ast_member(ast_index(ast_var(n->var_decl.name, n->line), ast_number(idx, n->line), n->line),
                                                       ff->field.name, 0, n->line),
                                            ast_number(ei, n->line), n->line);
                                        ASTNode *asg = ast_assign("=", lhs, sub, n->line);
                                        ASTNode *stmt = ast_expr_stmt(asg, n->line);
                                        codegen_stmt(cg, stmt);
                                    }
                                    continue;
                                }
                                ASTNode *sub = n->var_decl.init->block.stmts[flat];
                                if (!sub || sub->kind==AST_BLOCK) { flat++; continue; }
                                ASTNode *lhs = ast_member(
                                    ast_index(ast_var(n->var_decl.name, n->line), ast_number(idx, n->line), n->line),
                                    ff->field.name, 0, n->line);
                                ASTNode *asg = ast_assign("=", lhs, sub, n->line);
                                ASTNode *stmt = ast_expr_stmt(asg, n->line);
                                codegen_stmt(cg, stmt);
                                flat++;
                            }
                        }
                    }
                }
                if (handled_struct_array) {
                    /* Struct/union array elements fully expanded above. */
                } else {
                /* array initialiser: store each element at its natural element size.
                 * MUST use sizeof_type_sym (which resolves typedefs through the
                 * symbol table), not typeinfo_size (which only knows hardcoded
                 * builtin names and silently defaults to 4 for anything else,
                 * e.g. "wchar_t" — an auto/stack "wchar_t arr[N] = {...};"
                 * brace initializer was storing each 2-byte element 4 bytes
                 * apart, leaving every other wchar_t slot corrupt/uninitialized
                 * and the array's real tail never written at all). Must pass
                 * a COPY with array_size cleared, not effective_type directly:
                 * sizeof_type_sym multiplies by array_size when set (that's
                 * how it computes a whole array's total size elsewhere), so
                 * passing the array's own TypeInfo — whose array_size is the
                 * element COUNT — returned element_size*count (the whole
                 * array's byte size) as "the element size", making every
                 * element's stride equal to the array's total size. E.g.
                 * "int S[] = {1,2,4,8,16};" (5 ints, stride should be 4)
                 * instead strode by 5*4=20 bytes per element, blowing straight
                 * through the stack frame and stomping the caller's own
                 * parameter slot a few elements in. */
                TypeInfo esz_ti = *effective_type;
                esz_ti.array_size = -1;
                int esz = effective_type ? sizeof_type_sym(&esz_ti, cg->is_64bit, cg->sym) : 4;
                if (esz < 1) esz = 4;
                for (int i=0;i<n->var_decl.init->block.count&&i<arr;i++) {
                    codegen_expr(cg,n->var_decl.init->block.stmts[i]);
                    /* Use correct-width store based on element size */
                    if (esz==1) {
                        /* MOV BYTE PTR [RBP+disp], AL */
                        int disp = sym->offset + i;
                        if (cg->is_64bit) {
                            if (disp >= -128 && disp <= 127) {
                                asm_emit3(a,0x88,0x45,(uint8_t)(int8_t)disp);
                            } else {
                                asm_emit2(a,0x88,0x85);
                                asm_emit4(a,(uint8_t)disp,(uint8_t)(disp>>8),(uint8_t)(disp>>16),(uint8_t)(disp>>24));
                            }
                        } else {
                            if (disp >= -128 && disp <= 127) {
                                asm_emit3(a,0x88,0x45,(uint8_t)(int8_t)disp);
                            } else {
                                asm_emit2(a,0x88,0x85);
                                asm_emit4(a,(uint8_t)disp,(uint8_t)(disp>>8),(uint8_t)(disp>>16),(uint8_t)(disp>>24));
                            }
                        }
                    } else if (esz==2) {
                        /* MOV WORD PTR [RBP+disp], AX */
                        int disp = sym->offset + i*2;
                        asm_emit1(a,0x66); /* operand size prefix */
                        if (disp >= -128 && disp <= 127) {
                            asm_emit3(a,0x89,0x45,(uint8_t)(int8_t)disp);
                        } else {
                            asm_emit2(a,0x89,0x85);
                            asm_emit4(a,(uint8_t)disp,(uint8_t)(disp>>8),(uint8_t)(disp>>16),(uint8_t)(disp>>24));
                        }
                    } else if (esz==4) {
                        /* MOV DWORD PTR [RBP+disp], EAX */
                        int disp = sym->offset + i*4;
                        if (disp >= -128 && disp <= 127) {
                            asm_emit3(a,0x89,0x45,(uint8_t)(int8_t)disp);
                        } else {
                            asm_emit2(a,0x89,0x85);
                            asm_emit4(a,(uint8_t)disp,(uint8_t)(disp>>8),(uint8_t)(disp>>16),(uint8_t)(disp>>24));
                        }
                    } else {
                        /* Default: 8-byte store */
                        asm_mov_mem_reg(a,REG_RBP,sym->offset+i*esz,REG_RAX);
                    }
                }
                }
            } else if (n->var_decl.init->kind==AST_BLOCK) {
                /* Local (stack) struct/union variable with a brace initializer
                 * and arr<=0 (a single struct, not an array) — e.g.
                 * "CountEnvStringsData countdata = { 0, 0 };" (SDL3's own
                 * SDL_getenv.c). Nothing above handles this shape at all: the
                 * array branch requires arr>0, so this fell all the way
                 * through to the old naive fallback below, which called
                 * codegen_expr() on the WHOLE AST_BLOCK (evaluating it as if
                 * it were some kind of single expression — in practice just
                 * running each sub-element's side effects and keeping the
                 * LAST one's value in RAX) and stored that ONE value at the
                 * struct's base offset with a generic per-type width. Every
                 * field but whatever the store's width happened to cover was
                 * left as pure uninitialized stack garbage — confirmed via a
                 * real repro: countdata.count read 0 (the low bytes the one
                 * store happened to touch) while countdata.length (the next
                 * 8-byte field over) read raw garbage, which then fed a huge
                 * bogus size into the following SDL_malloc() call ("Out of
                 * memory" — a real allocation failure, not corruption; this
                 * was the root cause chased since the process-backend work).
                 * Fixed by resolving the struct/union type (same typedef
                 * fallback used by the global Pass-0.5 rewriter below) and
                 * generating one member-assignment statement per field,
                 * executed immediately via codegen_stmt() — reusing the
                 * ordinary AST_ASSIGN/AST_MEMBER codegen (which already gets
                 * per-field widths right) instead of inventing new asm here. */
                TypeInfo *bt = effective_type;
                const char *base = bt ? bt->base : NULL;
                int is_struct_base = base && bt->pointer_depth==0 &&
                    (strncmp(base,"struct ",7)==0 || strncmp(base,"union ",6)==0);
                if (!is_struct_base && base && bt->pointer_depth==0) {
                    Symbol *btd = symtable_lookup(cg->sym, base);
                    if (btd && btd->kind==SYM_TYPEDEF && btd->type && btd->type->base &&
                        (strncmp(btd->type->base,"struct ",7)==0 || strncmp(btd->type->base,"union ",6)==0)) {
                        base = btd->type->base;
                        is_struct_base = 1;
                    }
                }
                Symbol *ssym = is_struct_base ? symtable_lookup(cg->sym, base) : NULL;
                if (ssym && ssym->struct_node) {
                    ASTNode *sd = ssym->struct_node;
                    for (int fi=0; fi<sd->struct_decl.nfields && fi<n->var_decl.init->block.count; fi++) {
                        ASTNode *ff = sd->struct_decl.fields[fi];
                        ASTNode *elem = n->var_decl.init->block.stmts[fi];
                        if (!ff || ff->kind!=AST_FIELD || !ff->field.name) continue;
                        if (!elem || elem->kind==AST_BLOCK) continue; /* nested aggregate: leave zeroed */
                        ASTNode *lhs = ast_member(ast_var(n->var_decl.name, n->line), ff->field.name, 0, n->line);
                        ASTNode *asg = ast_assign("=", lhs, elem, n->line);
                        ASTNode *stmt = ast_expr_stmt(asg, n->line);
                        codegen_stmt(cg, stmt);
                    }
                } else if (n->var_decl.init->block.count >= 1) {
                    /* Not a struct/union — e.g. "int x = {5};", a legal single-
                     * element scalar brace init. Preserve old behavior. */
                    codegen_expr(cg, n->var_decl.init->block.stmts[0]);
                    if (cg->is_64bit && sym->type && sym->type->pointer_depth == 0) {
                        int vsz = typeinfo_size(sym->type, 1);
                        if      (vsz == 1) asm_mov_mem8_reg  (a,REG_RBP,sym->offset,REG_RAX);
                        else if (vsz <= 4) asm_mov_mem32_reg (a,REG_RBP,sym->offset,REG_RAX);
                        else               asm_mov_mem_reg   (a,REG_RBP,sym->offset,REG_RAX);
                    } else {
                        asm_mov_mem_reg(a,REG_RBP,sym->offset,REG_RAX);
                    }
                }
            } else {
                /* Struct/union-typed local declared with a plain (non-brace)
                 * initializer expression — "SDL_Rect viewport =
                 * renderer->view->pixel_viewport;" (SDL3's own real
                 * QueueCmdSetViewport()) is the textbook case: a struct-typed
                 * VAR_DECL initializer reached through a chain of pointer/
                 * member accesses. The generic fallback below evaluates the
                 * initializer with codegen_expr() (which for a struct-typed
                 * expression leaves at most one register's worth of bytes in
                 * RAX — in practice whatever garbage a struct-typed
                 * "expression value" degenerates to, since squash has no
                 * concept of a multi-word value living in a single register)
                 * and stores only that into the destination, leaving every
                 * byte after the first 4/8 as uninitialized stack garbage.
                 * This is the exact same bug class already fixed for whole-
                 * struct ASSIGNMENT ("a = b;"/"a = *b;"/"outer.field = v;",
                 * see struct_copy_size_of()'s own comment above) — just never
                 * covered for a VAR_DECL's own initializer, a distinct
                 * codegen path. Reuses the same struct_copy_size_of/
                 * struct_copy_addr_of helpers; the destination here is always
                 * the fixed, already-known local slot at RBP+sym->offset, so
                 * no destination-address computation is needed (unlike the
                 * assign path, which must compute both sides' addresses). */
                int decl_struct_sz = 0;
                if (effective_type && effective_type->pointer_depth==0 && effective_type->base) {
                    const char *dbase = effective_type->base;
                    int is_struct_base = (strncmp(dbase,"struct ",7)==0 || strncmp(dbase,"union ",6)==0);
                    if (!is_struct_base) {
                        Symbol *dbtd = symtable_lookup(cg->sym, dbase);
                        if (dbtd && dbtd->kind==SYM_TYPEDEF && dbtd->type && dbtd->type->base &&
                            (strncmp(dbtd->type->base,"struct ",7)==0 || strncmp(dbtd->type->base,"union ",6)==0))
                            is_struct_base = 1;
                    }
                    if (is_struct_base) decl_struct_sz = sizeof_type_sym(effective_type, cg->is_64bit, cg->sym);
                }
                int init_struct_sz = decl_struct_sz>0 ? struct_copy_size_of(cg, n->var_decl.init) : 0;
                if (decl_struct_sz>0 && init_struct_sz==decl_struct_sz) {
                    struct_copy_addr_of(cg, n->var_decl.init);   /* RAX = src addr */
                    asm_mov_reg_reg(a, REG_RBX, REG_RAX);        /* RBX = src addr (survives RCX/RDX use below) */
                    int off = 0;
                    for (; off+8<=decl_struct_sz; off+=8) {
                        asm_mov_reg_mem(a, REG_RCX, REG_RBX, off);
                        asm_mov_mem_reg(a, REG_RBP, sym->offset+off, REG_RCX);
                    }
                    for (; off+4<=decl_struct_sz; off+=4) {
                        asm_mov_reg32_mem(a, REG_RCX, REG_RBX, off);
                        asm_mov_mem32_reg(a, REG_RBP, sym->offset+off, REG_RCX);
                    }
                    if (off < decl_struct_sz) {
                        asm_mov_reg_reg(a, REG_RDX, REG_RBX);
                        for (; off<decl_struct_sz; off++) {
                            asm_movzx_eax_mem8(a, REG_RDX, off);
                            asm_mov_mem8_reg(a, REG_RBP, sym->offset+off, REG_RAX);
                        }
                    }
                } else {
                    codegen_expr(cg,n->var_decl.init);
                    /* Use size-aware store to avoid corrupting adjacent stack vars */
                    if (cg->is_64bit && sym->type && sym->type->pointer_depth == 0) {
                        int vsz = typeinfo_size(sym->type, 1);
                        if      (vsz == 1) asm_mov_mem8_reg  (a,REG_RBP,sym->offset,REG_RAX);
                        else if (vsz <= 4) asm_mov_mem32_reg (a,REG_RBP,sym->offset,REG_RAX);
                        else               asm_mov_mem_reg   (a,REG_RBP,sym->offset,REG_RAX);
                    } else {
                        asm_mov_mem_reg(a,REG_RBP,sym->offset,REG_RAX);
                    }
                }
            }
        } else {
            /* Zero-initialize: use size-aware store */
            asm_mov_reg_imm(a,REG_RAX,0);
            if (cg->is_64bit && sym->type && sym->type->pointer_depth == 0) {
                int vsz = typeinfo_size(sym->type, 1);
                if      (vsz == 1) asm_mov_mem8_reg  (a,REG_RBP,sym->offset,REG_RAX);
                else if (vsz <= 4) asm_mov_mem32_reg (a,REG_RBP,sym->offset,REG_RAX);
                else               asm_mov_mem_reg   (a,REG_RBP,sym->offset,REG_RAX);
            } else {
                asm_mov_mem_reg(a,REG_RBP,sym->offset,REG_RAX);
            }
        }
        break;
    }

    case AST_ASSIGN:
        codegen_expr(cg,n);
        break;

    case AST_IF: {
        int else_lbl=asm_new_label(a,"if_else");
        int end_lbl =asm_new_label(a,"if_end");
        codegen_branch(cg,n->if_.cond,else_lbl,0); /* jump to else if false */
        codegen_stmt(cg,n->if_.then_);
        asm_jmp_label(a,end_lbl);
        asm_def_label(a,else_lbl);
        if (n->if_.else_) codegen_stmt(cg,n->if_.else_);
        asm_def_label(a,end_lbl);
        break;
    }

    case AST_WHILE: {
        int top=asm_new_label(a,"whl_top"), end=asm_new_label(a,"whl_end");
        int sv_end=cg->loop_end_label, sv_top=cg->loop_top_label;
        cg->loop_end_label=end; cg->loop_top_label=top;
        asm_def_label(a,top);
        codegen_branch(cg,n->while_.cond,end,0); /* jump to end if false */
        codegen_stmt(cg,n->while_.body);
        asm_jmp_label(a,top);
        asm_def_label(a,end);
        cg->loop_end_label=sv_end; cg->loop_top_label=sv_top;
        break;
    }

    case AST_DO_WHILE: {
        int top=asm_new_label(a,"do_top"), end=asm_new_label(a,"do_end");
        int sv_end=cg->loop_end_label, sv_top=cg->loop_top_label;
        cg->loop_end_label=end; cg->loop_top_label=top;
        asm_def_label(a,top);
        codegen_stmt(cg,n->do_while.body);
        codegen_expr(cg,n->do_while.cond);
        asm_test_reg_reg(a,REG_RAX,REG_RAX);
        asm_jcc_label(a,CC_NE,top);
        asm_def_label(a,end);
        cg->loop_end_label=sv_end; cg->loop_top_label=sv_top;
        break;
    }

    case AST_FOR: {
        int top=asm_new_label(a,"for_top"), end=asm_new_label(a,"for_end"), step=asm_new_label(a,"for_step");
        int sv_end=cg->loop_end_label, sv_top=cg->loop_top_label;
        cg->loop_end_label=end; cg->loop_top_label=step;
        if (n->for_.init) codegen_stmt(cg,n->for_.init);
        asm_def_label(a,top);
        if (n->for_.cond) {
            codegen_branch(cg,n->for_.cond,end,0); /* jump to end if false */
        }
        codegen_stmt(cg,n->for_.body);
        asm_def_label(a,step);
        if (n->for_.step) codegen_expr(cg,n->for_.step);
        asm_jmp_label(a,top);
        asm_def_label(a,end);
        cg->loop_end_label=sv_end; cg->loop_top_label=sv_top;
        break;
    }

    case AST_SWITCH: {
        int end=asm_new_label(a,"sw_end");
        int sv_sw=cg->switch_end_label, sv_end=cg->loop_end_label;
        cg->switch_end_label=end; cg->loop_end_label=end;
        codegen_expr(cg,n->switch_.expr);
        /* Save switch value while preserving 16-byte stack alignment.
         * push rax misaligns RSP by 8 and crashes function calls inside case bodies. */
        if (cg->is_64bit) {
            asm_sub_rsp(a, 16);
            asm_mov_mem_reg(a, REG_RSP, 0, REG_RAX);
        } else {
            asm_push_reg(a, REG_RAX);
        }
        /* Generate jump table: compare and jump to each case */
        int *case_labels=malloc(n->switch_.nc*sizeof(int));
        int default_lbl=-1;
        for (int i=0;i<n->switch_.nc;i++) {
            case_labels[i]=asm_new_label(a,"sw_case");
            if (n->switch_.cases[i]->kind==AST_DEFAULT) {
                default_lbl=case_labels[i];
            }
        }
        /* emit comparisons */
        for (int i=0;i<n->switch_.nc;i++) {
            if (n->switch_.cases[i]->kind==AST_CASE) {
                /* cmp [stack top], case_val */
                asm_mov_reg_mem(a,REG_RBX,REG_RSP,0); /* load switch value */
                asm_mov_reg_imm(a,REG_RAX,n->switch_.cases[i]->case_.value);
                asm_cmp_reg_reg(a,REG_RBX,REG_RAX);
                asm_jcc_label(a,CC_E,case_labels[i]);
            }
        }
        if (default_lbl>=0) asm_jmp_label(a,default_lbl);
        else asm_jmp_label(a,end);
        /* emit case bodies */
        for (int i=0;i<n->switch_.nc;i++) {
            asm_def_label(a,case_labels[i]);
            ASTNode *c=n->switch_.cases[i];
            ASTNode **body = c->kind==AST_CASE ? c->case_.body : c->default_.body;
            int nb        = c->kind==AST_CASE ? c->case_.nb   : c->default_.nb;
            for (int j=0;j<nb;j++) codegen_stmt(cg,body[j]);
        }
        free(case_labels);
        asm_def_label(a,end);
        if (cg->is_64bit) {
            asm_add_rsp(a, 16);
        } else {
            asm_pop_reg(a, REG_RAX);
        }
        cg->switch_end_label=sv_sw; cg->loop_end_label=sv_end;
        break;
    }

    case AST_BREAK:
        if (cg->loop_end_label>=0) asm_jmp_label(a,cg->loop_end_label);
        else diag_emit(DIAG_ERROR, n->line, cg->cur_func_name, NULL,
                        "'break' statement not inside a loop or switch");
        break;

    case AST_CONTINUE:
        if (cg->loop_top_label>=0) asm_jmp_label(a,cg->loop_top_label);
        else diag_emit(DIAG_ERROR, n->line, cg->cur_func_name, NULL,
                        "'continue' statement not inside a loop");
        break;

    case AST_GOTO: {
        /* Forward jump to a named label — allocate label if needed */
        char key[300]; snprintf(key,sizeof key,"lbl_%s",n->goto_.label);
        int lid=get_func_label(cg,key);
        asm_jmp_label(a,lid);
        break;
    }

    case AST_LABEL: {
        char key[300]; snprintf(key,sizeof key,"lbl_%s",n->label.name);
        int lid=get_func_label(cg,key);
        asm_def_label(a,lid);
        if (n->label.stmt) codegen_stmt(cg,n->label.stmt);
        break;
    }

    case AST_RETURN:
        if (n->ret.expr) {
            if (codegen_is_float_expr(cg, n->ret.expr)) {
                /* Float-returning function: result must be in XMM0 (64-bit)
                 * or ST0 (32-bit x87). */
                codegen_float_expr(cg, n->ret.expr);
                /* codegen_float_expr() always computes in double precision —
                 * if the function's declared return type is "float" (32-bit),
                 * XMM0 holds a 64-bit double bit pattern the caller will
                 * misread as a packed 32-bit float. Without this narrowing,
                 * ANY float-returning function whose return expression is
                 * more than a bare cast/variable (e.g. "return a / 255.0f;")
                 * returns garbage — confirmed via an isolated repro: even
                 * "return 10.0f / 2.0f;" (should be 5.0f) came back as raw
                 * double bits reinterpreted as float, silently corrupting
                 * every SDL_SetRenderDrawColor()-style Uint8-to-float color
                 * conversion in the whole SDL3 render pipeline (the actual
                 * root cause of the black/white-only rendering bug — see
                 * project notes). A bare "return (float)x;"/"return var;"
                 * happened to look correct only because those specific
                 * shapes route through codegen_expr's own (already-correct)
                 * narrowing elsewhere, never through this path. */
                if (cg->is_64bit && cg->cur_func_ret_is_float32) {
                    asm_cvtsd2ss(a, 0, 0);
                }
            } else {
                codegen_expr(cg, n->ret.expr);
            }
        } else {
            asm_mov_reg_imm(a, REG_RAX, 0);
        }
        asm_win64_callee_restore(a);
        asm_leave(a); asm_ret(a);
        break;

    case AST_NUMBER:
    case AST_FLOAT:
    case AST_STRING:
        /* Null/no-op statement (e.g. from empty semicolon) — ignore */
        break;

    case AST_TYPEDEF_DECL:
        /* Local typedef — symtable already updated by parser, nothing to emit */
        break;

    case AST_ENUM_DECL:
    case AST_ENUM_VAL:
        /* Enum declarations — values already in symtable */
        break;

    case AST_ASM_STMT:
        codegen_asm_x86(cg, n);
        break;

    default:
        diag_emit(DIAG_ERROR, n->line, cg->cur_func_name, NULL,
                  "internal: codegen_stmt has no handler for AST node kind %d (this is a squash bug, not an error in your source)",
                  n->kind);
    }
}

/* =========================================================================
 * codegen_func
 * ========================================================================= */
/* SysV (Linux/macOS) variadic functions: stdarg.h's va_list is a flat
 * "char*" that va_arg walks by decrementing 8 bytes at a time, starting
 * right after the last named parameter's own register-spill slot. That
 * naive walk only works while it stays inside the contiguous 6-slot
 * integer register-save area (offsets rbp-8..rbp-48) this prologue spills
 * below. The SysV ABI passes the 7th-and-later argument on the CALLER's
 * stack instead (arriving at the callee's rbp+16, rbp+24, ... — a
 * completely different, non-contiguous region), so once va_arg decrements
 * past the register-save area it was reading unrelated local-variable
 * stack slots instead of those stack-passed arguments, coming back as
 * garbage/NULL (confirmed via a minimal repro: a locally-defined variadic
 * function called with more than 6 total arguments). Rather than making
 * va_list ABI-accurate (a two-region struct, as real SysV specifies —
 * a much larger undertaking), this copies up to VA_STACK_COPY_SLOTS of
 * the caller's stack-passed arguments down into the SAME contiguous
 * negative-offset region right after the register-save area, so va_arg's
 * existing simple backward walk keeps working uniformly across the
 * 6-argument boundary. The copy always runs (regardless of how many
 * stack args the caller actually passed for THIS call) since the callee
 * has no way to know that count; reading a few slots past what was
 * actually passed is harmless unless the callee's own va_arg calls go
 * that far too, which is undefined behavior in C regardless. */
#define VA_STACK_COPY_SLOTS 8
void codegen_func(CodeGen *cg, ASTNode *n) {
    Assembler *a=cg->asm_;
    if (!n||n->kind!=AST_FUNC_DECL) return;
    if (!n->func.body) return; /* forward declaration */

    int lid=get_func_label(cg,n->func.name);
    asm_def_label(a,lid);
    if (getenv("SQUASH_FUNCADDR_DEBUG")) {
        fprintf(stderr, "[funcaddr] rva=0x%x name=%s\n", a->code_len + 0x1000, n->func.name);
    }

    /* Diagnostics-only context (see diag.c): every diagnostic emitted while
     * generating code for this function's body can now say "in function
     * 'foo'" instead of leaving the user to guess which function a bare
     * line number belongs to. */
    cg->cur_func_name = n->func.name;

    /* Track whether this function's declared return type is 32-bit "float"
     * (as opposed to "double") — see AST_RETURN's use of this flag for why:
     * codegen_float_expr() always computes in double precision (SSE2
     * addsd/subsd/mulsd/divsd), so a float-returning function needs its
     * XMM0 result narrowed with cvtsd2ss before returning. */
    cg->cur_func_ret_is_float32 = (n->func.ret_type && n->func.ret_type->pointer_depth==0 &&
                                   n->func.ret_type->base && strcmp(n->func.ret_type->base,"float")==0);

    symtable_push_scope(cg->sym);
    symtable_reset_locals(cg->sym);
    /* On Win64, asm_enter_deferred() reserves [rbp-8..rbp-56] for the 7
     * callee-saved registers it pushes (plus [rbp-64] padding) — locals
     * must be allocated STARTING BELOW that reserved region, or the first
     * local declared silently overlaps live saved-register storage. This
     * was the actual final piece of the callee-saved-register fix: without
     * it, a function with both a live local AND a loop that calls back
     * into external code (e.g. SDL3's own PRIVATE_PumpEvents: a "MSG msg;"
     * local plus PeekMessageA/TranslateMessage/DispatchMessageA in a loop)
     * had its local variable's memory silently aliased with RBX/RSI/RDI/
     * R12-R15's saved values — restoring the "wrong" (actually just
     * whatever the local's own current content was) data back into those
     * registers on return, corrupting the caller. See asm_enter_deferred's
     * own comment in assembler.c for the full story. */
    cg->sym->next_offset = !cg->is_64bit ? 0
        : (cg->is_linux ? -(SQ_SYSV_CALLEE_SAVE_WORDS*8) : -64);

    /* Register params in symbol table with correct 32-bit byte offsets */
    { int param_byte_off=0;
      for (int i=0;i<n->func.paramc;i++) {
        ASTNode *pr=n->func.params[i];
        if (pr->param.name)
            symtable_define_param(cg->sym,pr->param.name,pr->param.type,i,param_byte_off);
        int psz=typeinfo_size(pr->param.type,cg->sym->is_64bit);
        param_byte_off += (psz>4?psz:4);
      }
    }

    /* SysV (Linux) variadic functions: reserve extra stack slots (beyond
     * the one each named param's symtable_define_param() call already
     * reserves) for spilling any remaining incoming INTEGER argument
     * registers (up to the SysV ABI's 6-integer-register limit) not
     * already covered by a named parameter -- see this function's SysV
     * prologue spill code below for the actual spill and its own comment
     * for the full rationale. Must happen before the two-pass frame-size
     * computation (asm_enter_deferred below) so local variables never get
     * allocated on top of these slots. */
    if (cg->is_64bit && cg->is_linux && n->func.is_variadic) {
        int max_named = n->func.paramc<6?n->func.paramc:6;
        int int_cnt = 0;
        for (int i=0;i<max_named;i++) {
            ASTNode *pr=n->func.params[i];
            if (!(pr->param.type && typeinfo_is_float(pr->param.type))) int_cnt++;
        }
        /* Also reserve VA_STACK_COPY_SLOTS extra slots below the register
         * save area — see the matching copy loop below (right after the
         * register spill) for why. */
        int reserve_to = -(SQ_SYSV_CALLEE_SAVE_WORDS + max_named + (6-int_cnt) + 1 + VA_STACK_COPY_SLOTS) * 8;
        if (cg->sym->next_offset > reserve_to) cg->sym->next_offset = reserve_to;
    }

    /* Two-pass frame sizing: emit prologue with placeholder size=0,
     * compile the body (which defines locals in the symtable),
     * then patch the placeholder with the actual aligned frame size.
     * This eliminates the hardcoded sub rsp,0x108 that triggers AV. */
    int frame_patch = asm_enter_deferred(a, cg->chkstk_lbl); /* probe for both 32 and 64-bit */
    /* Spill register params FIRST — before CRT startup calls which clobber registers */
    if (cg->is_64bit) {
        /* Avoid static Reg arrays (squash can't init non-zero local statics). */
        if (cg->is_linux) {
            /* SysV: integer params in RDI,RSI,RDX,RCX,R8,R9; float params in XMM0-XMM7.
             * These are independent counters — mixed int+float signatures need both. */
#define PR_REG(k) ((k)==0?REG_RDI:(k)==1?REG_RSI:(k)==2?REG_RDX:(k)==3?REG_RCX:(k)==4?REG_R8:REG_R9)
            int int_cnt = 0; int flt_cnt = 0;
            int max_named = n->func.paramc<6?n->func.paramc:6;
            for (int i=0;i<max_named;i++) {
                ASTNode *pr=n->func.params[i];
                int is_float_param = pr->param.type && typeinfo_is_float(pr->param.type);
                if (is_float_param) {
                    asm_movsd_store(a, REG_RBP, -(SQ_SYSV_CALLEE_SAVE_WORDS+i+1)*8, flt_cnt);
                    flt_cnt++;
                } else {
                    asm_mov_mem_reg(a,REG_RBP,-(SQ_SYSV_CALLEE_SAVE_WORDS+i+1)*8,PR_REG(int_cnt));
                    int_cnt++;
                }
            }
            /* Variadic: also spill any remaining incoming INTEGER argument
             * registers (up to SysV's 6-integer-register limit) that
             * weren't already spilled above as named parameters, mirroring
             * the identical Windows fix in the else-branch below (see its
             * own comment): va_start/va_arg just walk memory contiguously
             * starting right after the last named parameter's own slot, so
             * every remaining incoming register argument must ALSO be
             * spilled there or va_arg reads stale stack contents (this is
             * exactly the bug: "int n = vsnprintf(buf,sizeof buf,fmt,ap)"
             * inside a user-defined "myfunc(fmt, ...)" read garbage for
             * its first vararg because nothing ever spilled it). Only
             * supports all-integer/pointer variadic argument lists (no
             * floating-point varargs) — squash's simplified va_list model
             * (stdarg.h's flat "char*" walking one contiguous region) has
             * no separate float reg-save area the way the real SysV ABI's
             * va_list struct does; every real call site needing this
             * (printf-style %d/%p/%s diagnostics forwarded through a
             * user-defined variadic wrapper) only ever passes integer/
             * pointer-shaped variadic arguments, so this covers what's
             * actually needed without the much larger undertaking of a
             * fully ABI-accurate va_list. */
            if (n->func.is_variadic) {
                for (int i=int_cnt;i<6;i++) {
                    asm_mov_mem_reg(a,REG_RBP,-(SQ_SYSV_CALLEE_SAVE_WORDS+max_named+1+(i-int_cnt))*8,PR_REG(i));
                }
                /* See VA_STACK_COPY_SLOTS's comment above codegen_func:
                 * copy the caller's stack-passed arguments (7th+ total
                 * argument, arriving at rbp+16, rbp+24, ...) down into the
                 * same contiguous negative-offset region so va_arg's flat
                 * backward walk keeps working past the register-save
                 * area. RAX is safe to clobber here — it's not one of the
                 * SysV argument registers and every param register has
                 * already been spilled to memory above. */
                int reg_slots = SQ_SYSV_CALLEE_SAVE_WORDS + max_named + (6-int_cnt);
                for (int k=0;k<VA_STACK_COPY_SLOTS;k++) {
                    asm_mov_reg_mem(a,REG_RAX,REG_RBP,16+k*8);
                    asm_mov_mem_reg(a,REG_RBP,-(reg_slots+1+k)*8,REG_RAX);
                }
            }
#undef PR_REG
        } else {
            /* Windows: RCX,RDX,R8,R9 (up to 4 params).
             * Variadic functions must spill ALL 4 integer register slots to
             * their shadow-space stack locations, not just the named
             * params: va_start/va_arg (stdarg.h) just walk memory
             * contiguously starting right after the last named parameter,
             * relying on every incoming register argument having been
             * stored there per the Windows x64 ABI. With only `paramc`
             * slots spilled, a call like my_log(fmt, "str") left RDX
             * (the variadic arg) never written to [rbp+24], so va_arg
             * read stale stack contents instead of the real argument. */
#define PR_REG(i) ((i)==0?REG_RCX:(i)==1?REG_RDX:(i)==2?REG_R8:REG_R9)
            int spill_count = n->func.is_variadic ? 4 : (n->func.paramc<4?n->func.paramc:4);
            for (int i=0;i<spill_count;i++) {
                ASTNode *pr = (i<n->func.paramc) ? n->func.params[i] : NULL;
                int is_float_param = pr && pr->param.type && typeinfo_is_float(pr->param.type);
                if (is_float_param) {
                    asm_movsd_store(a, REG_RBP, 16+i*8, i);
                } else {
                    asm_mov_mem_reg(a,REG_RBP,16+i*8,PR_REG(i));
                }
            }
#undef PR_REG
        }
    }
    /* If main() has argc/argv params:
     * Linux: argc in RDI, argv in RSI — already spilled above; no extra code needed.
     * Windows: parse GetCommandLineA to populate argc/argv. */
    if (strcmp(n->func.name,"main")==0 && n->func.paramc >= 1 && !cg->is_linux) {
        symtable_add_import(cg->sym,"KERNEL32.dll:GetCommandLineA");
        symtable_add_import(cg->sym,"KERNEL32.dll:GetProcessHeap");
        symtable_add_import(cg->sym,"KERNEL32.dll:HeapAlloc");
        if (cg->is_64bit) {
            /* Parse Windows cmdline → real argc/argv.
             * R15 saves RSP so AND RSP,-16 doesn't corrupt the frame.
             * [rbp+16]=argc, [rbp+24]=cmdline/argv temp between calls. */
            int sk1=asm_new_label(a,"cl_sk1"); int tk1=asm_new_label(a,"cl_tk1");
            int in1=asm_new_label(a,"cl_in1"); int dn1=asm_new_label(a,"cl_dn1");
            int sk2=asm_new_label(a,"cl_sk2"); int tk2=asm_new_label(a,"cl_tk2");
            int in2=asm_new_label(a,"cl_in2"); int dn2=asm_new_label(a,"cl_dn2");


            /* Step 1: GetCommandLineA → cmdline in [rbp+24] */
            asm_sub_rsp(a,40); asm_call_import(a,"GetCommandLineA"); asm_add_rsp(a,40);
            asm_emit3(a,0x48,0x89,0x45); asm_emit1(a,0x18); /* mov [rbp+24],rax */

            /* Step 2: count tokens → argc in [rbp+16] */
            asm_emit3(a,0x48,0x8B,0x45); asm_emit1(a,0x18); /* mov rax,[rbp+24] */
            asm_emit3(a,0x48,0x31,0xC9);                    /* xor rcx,rcx */
            asm_def_label(a,sk1);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x20); asm_jcc_label(a,CC_NE,tk1);
            asm_emit3(a,0x48,0xFF,0xC0); asm_jmp_label(a,sk1); /* inc rax */
            asm_def_label(a,tk1);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x00); asm_jcc_label(a,CC_E,dn1);
            asm_emit2(a,0xFF,0xC1);
            asm_def_label(a,in1);
            asm_emit3(a,0x48,0xFF,0xC0); /* inc rax */
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x00); asm_jcc_label(a,CC_E,dn1);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x20); asm_jcc_label(a,CC_NE,in1);
            asm_jmp_label(a,sk1);
            asm_def_label(a,dn1);
            asm_emit3(a,0x48,0x89,0x4D); asm_emit1(a,0x10); /* mov [rbp+16],rcx (argc) */

            /* Step 3: HeapAlloc(GetProcessHeap(),0,(argc+1)*8) → argv in [rbp+24] */
            asm_sub_rsp(a,40); asm_call_import(a,"GetProcessHeap"); asm_add_rsp(a,40);
            asm_mov_reg_reg(a,REG_RCX,REG_RAX);
            asm_emit3(a,0x48,0x8B,0x45); asm_emit1(a,0x10); /* mov rax,[rbp+16] */
            asm_emit3(a,0x48,0x8D,0x40); asm_emit1(a,0x01); /* lea rax,[rax+1] */
            asm_emit4(a,0x48,0xC1,0xE0,0x03);               /* shl rax,3 */
            asm_mov_reg_reg(a,REG_R8,REG_RAX);
            asm_emit2(a,0x31,0xD2);                          /* xor edx,edx (flags=0) */
            asm_sub_rsp(a,40); asm_call_import(a,"HeapAlloc"); asm_add_rsp(a,40);
            asm_emit3(a,0x48,0x89,0x45); asm_emit1(a,0x18); /* mov [rbp+24],rax (argv) */

            /* Step 4: GetCommandLineA again for Pass 2 tokenisation */
            asm_sub_rsp(a,40); asm_call_import(a,"GetCommandLineA"); asm_add_rsp(a,40);
            asm_emit3(a,0x48,0x8B,0x4D); asm_emit1(a,0x18); /* mov rcx,[rbp+24] (argv) */
            asm_emit2(a,0x31,0xD2);                          /* xor edx,edx (tok_idx) */

            /* Pass 2: record token pointers, NUL-terminate in-place */
            asm_def_label(a,sk2);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x20); asm_jcc_label(a,CC_NE,tk2);
            asm_emit3(a,0x48,0xFF,0xC0); asm_jmp_label(a,sk2); /* inc rax */
            asm_def_label(a,tk2);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x00); asm_jcc_label(a,CC_E,dn2);
            asm_emit4(a,0x48,0x89,0x04,0xD1);               /* mov [rcx+rdx*8],rax */
            asm_emit2(a,0xFF,0xC2);                          /* inc edx */
            asm_def_label(a,in2);
            asm_emit3(a,0x48,0xFF,0xC0); /* inc rax */
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x00); asm_jcc_label(a,CC_E,dn2);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x20); asm_jcc_label(a,CC_NE,in2);
            asm_emit2(a,0xC6,0x00); asm_emit1(a,0x00);      /* NUL-terminate token */
            asm_emit3(a,0x48,0xFF,0xC0); asm_jmp_label(a,sk2); /* inc rax */
            asm_def_label(a,dn2);
            asm_emit3(a,0x4D,0x31,0xC0);                    /* xor r8,r8 */
            asm_emit4(a,0x4C,0x89,0x04,0xD1);               /* argv[tok_idx]=NULL */

            /* Restore RSP from R15, pop R15 */

            /* [rbp+16]=argc, [rbp+24]=argv */
        } else {
            /* 32-bit: EBX=cmdline, EDI=argc, ESI=argv, EAX=scan, EDX=tok_idx */
            int sk1b=asm_new_label(a,"cl32_sk1"),tk1b=asm_new_label(a,"cl32_tk1");
            int in1b=asm_new_label(a,"cl32_in1"),dn1b=asm_new_label(a,"cl32_dn1");
            int sk2b=asm_new_label(a,"cl32_sk2"),tk2b=asm_new_label(a,"cl32_tk2");
            int in2b=asm_new_label(a,"cl32_in2"),dn2b=asm_new_label(a,"cl32_dn2");
            asm_push_reg(a,REG_EBX); asm_push_reg(a,REG_ESI); asm_push_reg(a,REG_EDI);
            asm_call_import32(a,"GetCommandLineA"); asm_mov_reg_reg(a,REG_EBX,REG_EAX);
            asm_mov_reg_reg(a,REG_EAX,REG_EBX); asm_emit2(a,0x31,0xFF);
            asm_def_label(a,sk1b);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x20); asm_jcc_label(a,CC_NE,tk1b);
            asm_emit2(a,0xFF,0xC0); asm_jmp_label(a,sk1b);
            asm_def_label(a,tk1b);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x00); asm_jcc_label(a,CC_E,dn1b);
            asm_emit2(a,0xFF,0xC7);
            asm_def_label(a,in1b);
            asm_emit2(a,0xFF,0xC0);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x00); asm_jcc_label(a,CC_E,dn1b);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x20); asm_jcc_label(a,CC_NE,in1b);
            asm_jmp_label(a,sk1b);
            asm_def_label(a,dn1b); /* EDI=argc */
            /* HeapAlloc(GetProcessHeap(),8,(edi+1)*4) */
            asm_call_import32(a,"GetProcessHeap"); /* EAX=heap */
            asm_emit2(a,0x8D,0x47); asm_emit1(a,0x01); /* lea eax,[edi+1] */
            asm_emit3(a,0xC1,0xE0,0x02); /* shl eax,2 -> size */
            asm_push_reg(a,REG_EAX); /* push size */
            asm_emit1(a,0x6A); asm_emit1(a,0x08); /* push 8 (HEAP_ZERO) */
            asm_call_import32(a,"GetProcessHeap"); /* push handle */
            asm_push_reg(a,REG_EAX);
            asm_call_import32(a,"HeapAlloc"); /* stdcall pops 3 args */
            asm_mov_reg_reg(a,REG_ESI,REG_EAX); /* ESI=argv */
            asm_mov_reg_reg(a,REG_EAX,REG_EBX); asm_emit2(a,0x31,0xD2);
            asm_def_label(a,sk2b);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x20); asm_jcc_label(a,CC_NE,tk2b);
            asm_emit2(a,0xFF,0xC0); asm_jmp_label(a,sk2b);
            asm_def_label(a,tk2b);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x00); asm_jcc_label(a,CC_E,dn2b);
            asm_emit3(a,0x89,0x04,0x96); asm_emit2(a,0xFF,0xC2);
            asm_def_label(a,in2b);
            asm_emit2(a,0xFF,0xC0);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x00); asm_jcc_label(a,CC_E,dn2b);
            asm_emit2(a,0x80,0x38); asm_emit1(a,0x20); asm_jcc_label(a,CC_NE,in2b);
            asm_emit2(a,0xC6,0x00); asm_emit1(a,0x00); asm_emit2(a,0xFF,0xC0); asm_jmp_label(a,sk2b);
            asm_def_label(a,dn2b);
            asm_emit3(a,0xC7,0x04,0x96); asm_emit_u32(a,0);
            asm_emit3(a,0x89,0x7D,0x08); asm_emit3(a,0x89,0x75,0x0C); /* argc,argv */
            asm_pop_reg(a,REG_EDI); asm_pop_reg(a,REG_ESI); asm_pop_reg(a,REG_EBX);
        }
    }
    /* Generate function body; locals accumulate into sym->next_offset */
    codegen_stmt(cg,n->func.body);

    /* Default return 0 at fall-through */
    asm_mov_reg_imm(a,REG_RAX,0);
    asm_win64_callee_restore(a);
    asm_leave(a); asm_ret(a);

    /* Patch the frame size with the actual number of bytes needed.
     * Linux SysV: keep align ≡ 0 (mod 16); call dispatch frames also 0 mod 16 → sum = 0 at CALL.
     * Windows x64: keep align ≡ 8 (mod 16); call dispatch adds 8 → sum = 0 at CALL. */
    int raw   = symtable_local_size(cg->sym);
    int align = (raw + 15) & ~15;
    if (cg->is_64bit) {
        if (!cg->is_linux && (align & 8) == 0) align += 8;
        if (align < 8) align = 8;
        if (cg->is_linux) { if (align < 32) align = 32; }
        else              { if (align < 24) align = 24; }
    } else {
        if ((align & 8) == 0) align += 8;
        if (align < 8) align = 8;
    }
    asm_patch_frame(a, frame_patch, align);

    symtable_pop_scope(cg->sym);
}

/* =========================================================================
 * codegen_program
 * ========================================================================= */
void codegen_program(CodeGen *cg, ASTNode *prog) {
    if (!prog||prog->kind!=AST_PROGRAM) return;

    /* Pass 0: register all global/static variables in wdata FIRST, so that
     * function codegens can reference them via asm_reloc_wdata.            */
    for (int i=0;i<prog->program.count;i++) {
        ASTNode *d=prog->program.decls[i];
        if (!d || d->kind!=AST_VAR_DECL) continue;
        const char *vname = d->var_decl.name;
        if (!vname || !vname[0]) continue;  /* skip unnamed struct/typedef decls */
        TypeInfo   *vtype = d->var_decl.type;
        int vsz;
        { int varr = d->var_decl.array_size;
          if (varr <= 0 && vtype) varr = vtype->array_size;
          if (varr > 0) {
              /* Array: element size * count. Must use sizeof_type_sym (which
               * resolves struct/union/typedef sizes via the symbol table),
               * not typeinfo_size — typeinfo_size hardcodes 4 bytes for any
               * non-primitive base type ("default for structs etc"), so any
               * global array of structs (e.g. SDL_iconv.c's "encodings[]",
               * SDL3's many case_fold and SDL_vidpid_list tables) was allocated
               * a tiny fraction of its real size, silently corrupting
               * whatever wdata was allocated right after it once indices
               * past the first couple were written. This was a real,
               * pervasive STATUS_ACCESS_VIOLATION-class bug, not SDL3's. */
              if (vtype) {
                  /* sizeof_type_sym already multiplies by array_size itself
                   * when set (it's also used directly on array-typed
                   * TypeInfos elsewhere) — strip it here first so we get
                   * just the per-element size, since this branch does its
                   * own "* varr" below using whichever of
                   * d->var_decl.array_size / vtype->array_size was set. */
                  TypeInfo elem_ti = *vtype;
                  elem_ti.array_size = 0;
                  vsz = sizeof_type_sym(&elem_ti, cg->is_64bit, cg->sym);
              } else {
                  vsz = 4;
              }
              if (vsz < 1) vsz = 1;
              vsz *= varr;
              /* T x[N][M]: a second dimension multiplies the element count
               * again — e.g. SDL3's own "static const SDL_AudioFormat
               * format_list[NUM_FORMATS][NUM_FORMATS + 1]" (SDL_audio.c).
               * Missing this left the wdata slot sized for only the FIRST
               * dimension's worth of elements, so Pass 0.5's real
               * initializer-flattening (which does write out all N*M
               * elements) silently overran into whatever wdata was
               * allocated immediately after — the exact "sized array
               * for a fraction of its real size, corrupting adjacent
               * wdata" bug class already fixed above for struct/union
               * element types, just for a second array dimension instead
               * of a wrong per-element size. */
              if (vtype->array_size2 > 0) vsz *= vtype->array_size2;
          } else {
              /* Non-array: sizeof_type_sym resolves structs/typedefs correctly */
              vsz = vtype ? sizeof_type_sym(vtype, cg->is_64bit, cg->sym) : 4;
          }
          if (vsz < 1) vsz = 4; }
        int already=0;
        for(int wi=0;wi<cg->wdata_count;wi++)
            if(cg->wdata[wi].label && strcmp(cg->wdata[wi].label,vname)==0){already=1;break;}
        if(!already) intern_wdata(cg, vname, vsz);
    }


    /* Pre-allocate stdout handle slot so all functions can reference it.
     * The slot itself is initialized in main() at runtime. */
    if (cg->stdout_handle_lbl[0]=='\0') {
        snprintf(cg->stdout_handle_lbl,sizeof cg->stdout_handle_lbl,"__stdout_h");
        intern_wdata(cg,cg->stdout_handle_lbl, cg->is_64bit ? 8 : 4);
    }
    /* Emit embedded __chkstk_probe helper at the start of .text.
     * Probes stack guard pages one page at a time before sub rsp/esp,N so
     * Windows can extend the stack for large frames without a guard-page fault.
     * Input: eax = frame size in bytes. Does NOT modify rsp/esp. */
    if (cg->is_64bit) {
        Assembler *a = cg->asm_;
        cg->chkstk_lbl = asm_new_label(a, "__chkstk_probe");
        asm_def_label(a, cg->chkstk_lbl);
        /* Byte sequence (41 bytes):
         *   push r10                        ; 41 52
         *   cmp rax, 0x1000                 ; 48 3D 00 10 00 00
         *   jle +28 (to pop r10 / ret)      ; 7E 1C
         *   mov r10, rsp                    ; 4C 8B D4
         * loop:
         *   sub r10, 4096                   ; 49 81 EA 00 10 00 00
         *   or dword ptr [r10], 0           ; 41 83 0A 00  (touch page)
         *   sub rax, 4096                   ; 48 2D 00 10 00 00
         *   cmp rax, 0x1000                 ; 48 3D 00 10 00 00
         *   jg -25 (to loop)                ; 7F E7
         * done:
         *   pop r10                         ; 41 5A
         *   ret                             ; C3                          */
        /* Emit __chkstk_probe inline — avoid static byte array (squash can't init them). */
        asm_emit1(a,0x41); asm_emit1(a,0x52);                              /* push r10        */
        asm_emit1(a,0x48); asm_emit1(a,0x3D);                              /* cmp rax, 0x1000 */
        asm_emit1(a,0x00); asm_emit1(a,0x10); asm_emit1(a,0x00); asm_emit1(a,0x00);
        asm_emit1(a,0x7E); asm_emit1(a,0x1C);                              /* jle +28 (done)  */
        asm_emit1(a,0x4C); asm_emit1(a,0x8B); asm_emit1(a,0xD4);          /* mov r10, rsp    */
        /* loop: */
        asm_emit1(a,0x49); asm_emit1(a,0x81); asm_emit1(a,0xEA);          /* sub r10, 4096   */
        asm_emit1(a,0x00); asm_emit1(a,0x10); asm_emit1(a,0x00); asm_emit1(a,0x00);
        asm_emit1(a,0x41); asm_emit1(a,0x83); asm_emit1(a,0x0A); asm_emit1(a,0x00); /* or [r10],0 */
        asm_emit1(a,0x48); asm_emit1(a,0x2D);                              /* sub rax, 4096   */
        asm_emit1(a,0x00); asm_emit1(a,0x10); asm_emit1(a,0x00); asm_emit1(a,0x00);
        asm_emit1(a,0x48); asm_emit1(a,0x3D);                              /* cmp rax, 0x1000 */
        asm_emit1(a,0x00); asm_emit1(a,0x10); asm_emit1(a,0x00); asm_emit1(a,0x00);
        asm_emit1(a,0x7F); asm_emit1(a,0xE7);                              /* jg loop (-25)   */
        /* done: */
        asm_emit1(a,0x41); asm_emit1(a,0x5A);                              /* pop r10         */
        asm_emit1(a,0xC3);                                                  /* ret             */
    } else {
        /* 32-bit __chkstk_probe32 (31 bytes):
         * Input: eax = frame size. Clobbers: ecx. Does NOT modify esp.
         *   cmp eax, 0x1000                 ; 3D 00 10 00 00
         *   jle +23 (done)                  ; 7E 17
         *   mov ecx, esp                    ; 89 E1
         * loop:
         *   sub ecx, 0x1000                 ; 81 E9 00 10 00 00
         *   or dword ptr [ecx], 0           ; 83 09 00   (touch page)
         *   sub eax, 0x1000                 ; 2D 00 10 00 00
         *   cmp eax, 0x1000                 ; 3D 00 10 00 00
         *   jg -21 (loop)                   ; 7F EB
         * done:
         *   ret                             ; C3                          */
        Assembler *a = cg->asm_;
        cg->chkstk_lbl = asm_new_label(a, "__chkstk_probe32");
        asm_def_label(a, cg->chkstk_lbl);
        asm_emit1(a,0x3D); asm_emit1(a,0x00); asm_emit1(a,0x10);          /* cmp eax,0x1000  */
        asm_emit1(a,0x00); asm_emit1(a,0x00);
        asm_emit1(a,0x7E); asm_emit1(a,0x17);                              /* jle done (+23)  */
        asm_emit1(a,0x89); asm_emit1(a,0xE1);                              /* mov ecx,esp     */
        /* loop: */
        asm_emit1(a,0x81); asm_emit1(a,0xE9); asm_emit1(a,0x00);          /* sub ecx,0x1000  */
        asm_emit1(a,0x10); asm_emit1(a,0x00); asm_emit1(a,0x00);
        asm_emit1(a,0x83); asm_emit1(a,0x09); asm_emit1(a,0x00);          /* or [ecx],0      */
        asm_emit1(a,0x2D); asm_emit1(a,0x00); asm_emit1(a,0x10);          /* sub eax,0x1000  */
        asm_emit1(a,0x00); asm_emit1(a,0x00);
        asm_emit1(a,0x3D); asm_emit1(a,0x00); asm_emit1(a,0x10);          /* cmp eax,0x1000  */
        asm_emit1(a,0x00); asm_emit1(a,0x00);
        asm_emit1(a,0x7F); asm_emit1(a,0xEB);                              /* jg loop (-21)   */
        /* done: */
        asm_emit1(a,0xC3);                                                  /* ret             */
    }

    /* Pass 0.5: global variables with a non-trivial initializer need runtime
     * code to set them up — e.g. a string-literal pointer requires a
     * RIP-relative LEA, not just static bytes, so "static const char *s =
     * "hi";" left `s` permanently NULL (Pass 0 only reserves and zeroes its
     * wdata slot; nothing ever wrote the initializer into it). Squash has no
     * "run before main" constructors section, so the simplest correct fix is
     * to run this set-up at the very start of main() itself.
     *
     * Array brace initializers (e.g. stdlib/SDL_iconv.c's "encodings[]")
     * were previously abandoned here after appearing to stay all-zero even
     * with a working-in-isolation expansion — that turned out to be a
     * DIFFERENT bug entirely: Pass 0's own wdata allocation sized array
     * elements via typeinfo_size(), which hardcodes 4 bytes for any
     * struct/union type, so every global array-of-struct in the whole
     * build (encodings[] included) was allocated a small fraction of its
     * real size and got its tail end silently overwritten by whatever
     * global was allocated next. Once THAT was fixed (see Pass 0 above,
     * "must use sizeof_type_sym"), this per-element expansion works
     * correctly and durably. */
    {
        int init_stmts_cap = 8192;
        ASTNode **init_stmts_buf = malloc(init_stmts_cap * sizeof(ASTNode*));
        int n_init_stmts = 0;
        for (int i=0;i<prog->program.count && n_init_stmts<init_stmts_cap;i++) {
            ASTNode *d=prog->program.decls[i];
            if (!d || d->kind!=AST_VAR_DECL) continue;
            if (!d->var_decl.init) continue;
            if (!d->var_decl.name || !d->var_decl.name[0]) continue;
            if (d->var_decl.init->kind==AST_BLOCK) {
                /* Struct/array brace initializer, e.g. SDL_malloc.c's own
                 * "static struct { ... } s_mem = { real_malloc, ... };" or
                 * stdlib/SDL_iconv.c's "static struct {...} encodings[] =
                 * { {"ASCII",...}, ... };" — calling through s_mem's NULL
                 * function pointers, or SDL_iconv_open() comparing against
                 * encodings[i].name when every entry was silently left
                 * all-zero (Pass 0 only zeroes the wdata slot; brace
                 * initializers were previously skipped entirely here), are
                 * both real STATUS_ACCESS_VIOLATIONs, not SDL bugs. Pass 0's
                 * zeroing already matches what a nested "{0}" element or an
                 * omitted trailing field/element wants, so only the
                 * non-aggregate (scalar/pointer) leaves need an explicit
                 * runtime assignment, generated positionally against the
                 * struct's own field list (and, for arrays, once per
                 * element too). */
                TypeInfo *vt = d->var_decl.type;
                if (!vt || !vt->base) continue;
                const char *base = vt->base;
                /* A POINTER-typed declaration (e.g. "static Driver
                 * *drivers[] = { &g_dummy_driver };" — each array ELEMENT
                 * is a Driver*, not a Driver by value) must never be
                 * treated as "is_struct_base", even though "Driver" itself
                 * names/resolves to a struct — the array holds POINTERS,
                 * so each initializer-list entry is one whole scalar
                 * (pointer) value per array slot, not one sub-field per
                 * slot. Without this check, "Driver *drivers[]" was
                 * misclassified as "array of Driver structs by value",
                 * taking the struct-expansion branch below (which expects
                 * `nf` flat sub-values per array element, matching
                 * Driver's own field count) instead of the correct
                 * one-value-per-slot branch — so the ONE real initializer
                 * value ("&g_dummy_driver") got silently treated as if it
                 * were the FIRST of several field values for array
                 * element 0, and no assignment to "drivers[0]" was ever
                 * actually generated, leaving it at Pass 0's zeroed
                 * default (NULL) — a guaranteed NULL-pointer dereference
                 * the moment anything indexed into and used the array
                 * (e.g. SDL3's own "SDL_joystick_drivers[]"/driver-list
                 * pattern, used throughout the codebase for pluggable
                 * subsystem backends). */
                int is_struct_base = (vt->pointer_depth == 0) &&
                    (strncmp(base,"struct ",7)==0 || strncmp(base,"union ",6)==0);
                if (!is_struct_base && vt->pointer_depth == 0) {
                    /* "base" may be a typedef NAME rather than a literal
                     * "struct "/"union " tag — e.g. "typedef struct {...}
                     * Bucket;" (an extremely common pattern; this is
                     * exactly how SDL3's own SDL_HashTable and its
                     * case-folding hash-bucket structs are declared)
                     * stores base="Bucket", not "struct $anonN". Every
                     * OTHER struct-resolution helper in this file
                     * (resolve_node_type, field_byte_offset, the
                     * AST_ASSIGN store-width lookup) already falls back to
                     * a typedef lookup here; this brace-initializer path
                     * didn't, so typedef'd struct/union globals with a
                     * brace initializer were silently skipped entirely —
                     * Pass 0's zeroed wdata slot was never subsequently
                     * written to, leaving every field (pointer fields
                     * included) permanently zero at runtime. */
                    Symbol *btd = symtable_lookup(cg->sym, base);
                    if (btd && btd->kind==SYM_TYPEDEF && btd->type && btd->type->base &&
                        (strncmp(btd->type->base,"struct ",7)==0 || strncmp(btd->type->base,"union ",6)==0)) {
                        base = btd->type->base;
                        is_struct_base = 1;
                    }
                }
                int arrn = d->var_decl.array_size>0 ? d->var_decl.array_size
                         : (vt->array_size>0 ? vt->array_size : 0);
                /* T x[N][M]: the parser flattens a 2D brace initializer into
                 * one flat N*M-element list (same flattening the struct-field
                 * comment below already describes), so the total element
                 * count this loop must cover is N*M, not just N — otherwise
                 * only the first "row" ever gets a real assignment and every
                 * later row stays at Pass 0's zeroed default. */
                if (vt->array_size2 > 0) arrn *= vt->array_size2;
                ASTNode *initlist = d->var_decl.init;
                /* Cap array expansion to modestly-sized tables — this
                 * generates one runtime statement per scalar leaf, so
                 * SDL3's largest lookup tables (some run past a thousand
                 * elements) would bloat main() by thousands of statements
                 * for marginal benefit. Larger tables keep the pre-existing
                 * all-zero-init limitation.
                 *
                 * Tried raising this to 8192 (a real, fully-specified
                 * 453-element uint32_t array — embedded SPIR-V shader
                 * bytecode for a Vulkan demo — silently read back as
                 * entirely zero past 256, which is what led here) but that
                 * broke SDL_InitSubSystem(VIDEO) ("No available video
                 * device") elsewhere in the real SDL3 build: some other
                 * large global initializer in SDL3's own source is a SPARSE
                 * one (C99 designated-initializer syntax, "[5] = x, [200] =
                 * y", with real gaps) that this flat, sequential-leaf
                 * expansion path isn't equipped to handle correctly —
                 * confirmed by reverting the cap back to 256, which
                 * immediately restored working video init. Left at 256
                 * (the known-safe value) rather than risk that regression;
                 * the large-sequential-array case (SPIR-V bytecode, lookup
                 * tables with no gaps) is real and worth fixing properly
                 * later, but needs the sparse-initializer case handled
                 * first, not just a bigger number here. */
                if (arrn>0 && arrn<=256) {
                    /* Array of N elements. IMPORTANT: the parser flattens
                     * nested per-element "{...}" braces into one flat list
                     * instead of nesting an AST_BLOCK per element (see its
                     * own comment: "nested brace: struct literal - collect
                     * until }" — collected straight into the same flat
                     * elems[] array), so for an array of structs the flat
                     * list is [f0,f1,f0,f1,...] (nfields per element), not
                     * [{f0,f1},{f0,f1},...]. arrn itself was inferred from
                     * this same flat count during parsing, so it's the
                     * flat length, not the true element count, whenever
                     * the element type is a struct/union. */
                    Symbol *esym = is_struct_base ? symtable_lookup(cg->sym, base) : NULL;
                    ASTNode *esd = (esym && esym->struct_node) ? esym->struct_node : NULL;
                    if (esd && esd->struct_decl.nfields>0) {
                        int nf = esd->struct_decl.nfields;
                        /* Per-element flat slot count: a scalar field is one
                         * flat slot, but a field that's ITSELF an array
                         * (e.g. WaveExtensibleGUID's "Uint8 guid[16];" in
                         * SDL3's own audio/SDL_wave.c) occupies array_size
                         * flat slots — the parser's brace flattener recurses
                         * into a field's own nested "{...}" the same as it
                         * does for the outer array-of-structs list, so the
                         * flat stream for one element is
                         * [f0, f1_0..f1_15, f2, ...], not a fixed nf-wide
                         * stride. Using a flat nf here (as if every field
                         * were exactly one slot) miscounted true_n and thus
                         * misaligned every element after the first whenever
                         * any field was array-typed. */
                        int per_elem_flat = 0;
                        for (int fi=0; fi<nf; fi++) {
                            ASTNode *ff0 = esd->struct_decl.fields[fi];
                            per_elem_flat += (ff0 && ff0->kind==AST_FIELD && ff0->field.array_size>0) ? ff0->field.array_size : 1;
                        }
                        if (per_elem_flat < 1) per_elem_flat = nf;
                        int true_n = initlist->block.count / per_elem_flat;
                        int flat = 0;
                        for (int idx=0; idx<true_n && n_init_stmts<init_stmts_cap; idx++) {
                            for (int fi=0; fi<nf && flat<initlist->block.count && n_init_stmts<init_stmts_cap; fi++) {
                                ASTNode *ff = esd->struct_decl.fields[fi];
                                if (!ff || ff->kind!=AST_FIELD || !ff->field.name) { flat++; continue; }
                                if (ff->field.array_size > 0) {
                                    for (int ei=0; ei<ff->field.array_size && flat<initlist->block.count && n_init_stmts<init_stmts_cap; ei++, flat++) {
                                        ASTNode *sub = initlist->block.stmts[flat];
                                        if (!sub || sub->kind==AST_BLOCK) continue;
                                        ASTNode *lhs = ast_index(
                                            ast_member(ast_index(ast_var(d->var_decl.name, d->line), ast_number(idx, d->line), d->line),
                                                       ff->field.name, 0, d->line),
                                            ast_number(ei, d->line), d->line);
                                        ASTNode *asg = ast_assign("=", lhs, sub, d->line);
                                        initlist->block.stmts[flat] = NULL;
                                        init_stmts_buf[n_init_stmts++] = ast_expr_stmt(asg, d->line);
                                    }
                                    continue;
                                }
                                ASTNode *sub = initlist->block.stmts[flat];
                                if (!sub || sub->kind==AST_BLOCK) { flat++; continue; }
                                ASTNode *lhs = ast_member(
                                    ast_index(ast_var(d->var_decl.name, d->line), ast_number(idx, d->line), d->line),
                                    ff->field.name, 0, d->line);
                                ASTNode *asg = ast_assign("=", lhs, sub, d->line);
                                initlist->block.stmts[flat] = NULL;
                                init_stmts_buf[n_init_stmts++] = ast_expr_stmt(asg, d->line);
                                flat++;
                            }
                        }
                    } else {
                        /* T x[N][M] (not an array of struct/union): arrn is
                         * the FLAT N*M element count (see the array_size2
                         * multiply just above), but "x[idx]" for idx up to
                         * N*M-1 is NOT the flat idx-th scalar — for a
                         * declared-2D array, a single index only selects a
                         * whole ROW (an M-element sub-array), so this used
                         * to silently generate "x[idx] = scalar" with idx
                         * running past the real row count N, storing each
                         * scalar at row-idx*sizeof(row) instead of its real
                         * flat byte offset (idx*sizeof(element)). Confirmed
                         * via a minimal repro (2x4 array of function
                         * pointers): only every 4th generated assignment
                         * landed near a correct slot, and every OTHER
                         * element silently stayed zeroed — this was the
                         * real root cause of a NULL-function-pointer call
                         * crash in real SDL3's channel_converters[8][8]
                         * (SDL_audio_channel_converters.h) once real audio
                         * data finally reached its call site. Use proper
                         * "x[idx/M][idx%M]" 2D indexing whenever a second
                         * dimension is declared. */
                        int dim2 = vt->array_size2 > 0 ? vt->array_size2 : 0;
                        for (int idx=0; idx<arrn && idx<initlist->block.count && n_init_stmts<init_stmts_cap; idx++) {
                            ASTNode *elem = initlist->block.stmts[idx];
                            if (!elem || elem->kind==AST_BLOCK) continue;
                            ASTNode *lhs;
                            if (dim2 > 0) {
                                ASTNode *row = ast_index(ast_var(d->var_decl.name, d->line), ast_number(idx/dim2, d->line), d->line);
                                lhs = ast_index(row, ast_number(idx%dim2, d->line), d->line);
                            } else {
                                lhs = ast_index(ast_var(d->var_decl.name, d->line), ast_number(idx, d->line), d->line);
                            }
                            ASTNode *asg = ast_assign("=", lhs, elem, d->line);
                            initlist->block.stmts[idx] = NULL;
                            init_stmts_buf[n_init_stmts++] = ast_expr_stmt(asg, d->line);
                        }
                    }
                    continue;
                }
                if (arrn>0) continue; /* array too large to expand — leave zeroed, as before */
                if (!is_struct_base) continue; /* only plain struct/union aggregates handled here */
                Symbol *ssym = symtable_lookup(cg->sym, base);
                if (!ssym || !ssym->struct_node) continue;
                ASTNode *sd = ssym->struct_node;
                /* IMPORTANT (same flattening the array-of-structs branch
                 * above already accounts for, via its own "flat" cursor):
                 * a struct FIELD that is itself an array — e.g. DEFINE_GUID's
                 * "GUID x = { l, w1, w2, { b1,...,b8 } };" (Data4[8]) — has
                 * its nested "{ b1,...,b8 }" brace flattened by the parser
                 * directly into THIS SAME top-level initializer list, not
                 * kept as a nested AST_BLOCK. So `initlist->block.count` for
                 * a struct with one 8-element array field among its 4 total
                 * fields is 11 (3 scalars + 8 flattened bytes), not 4 — a
                 * naive one-list-element-per-field walk (indexing
                 * initlist->block.stmts[fi] by FIELD index fi) silently
                 * takes only the array field's FIRST flattened element as if
                 * it were that field's single (wrong-typed, 8-byte) whole
                 * value, and never even looks at the remaining 7 elements.
                 * Confirmed via a real DEFINE_GUID-shaped repro: Data4[0]
                 * came through (from the scalar-assignment path, coincidence
                 * of it happening to be the first flattened byte) while
                 * Data4[1..7] stayed zero — corrupting every DEFINE_GUID'd
                 * constant used by SDL_GetUserFolder's shlobj.h
                 * infrastructure. Fixed by walking a separate FLAT cursor
                 * (like the sibling array-of-structs branch already does)
                 * instead of assuming fi indexes 1:1 into the flat list. */
                int flat = 0;
                for (int fi=0; fi<sd->struct_decl.nfields && flat<initlist->block.count && n_init_stmts<init_stmts_cap; fi++) {
                    ASTNode *ff = sd->struct_decl.fields[fi];
                    if (!ff || ff->kind!=AST_FIELD || !ff->field.name) { flat++; continue; }
                    if (ff->field.array_size > 0) {
                        for (int ei=0; ei<ff->field.array_size && flat<initlist->block.count && n_init_stmts<init_stmts_cap; ei++, flat++) {
                            ASTNode *subelem = initlist->block.stmts[flat];
                            if (!subelem || subelem->kind==AST_BLOCK) continue;
                            ASTNode *flhs = ast_index(
                                ast_member(ast_var(d->var_decl.name, d->line), ff->field.name, 0, d->line),
                                ast_number(ei, d->line), d->line);
                            ASTNode *fasg = ast_assign("=", flhs, subelem, d->line);
                            initlist->block.stmts[flat] = NULL;
                            init_stmts_buf[n_init_stmts++] = ast_expr_stmt(fasg, d->line);
                        }
                        continue;
                    }
                    ASTNode *elem = initlist->block.stmts[flat];
                    if (!elem || elem->kind==AST_BLOCK) { flat++; continue; } /* nested aggregate: already zeroed */
                    ASTNode *lhs = ast_member(ast_var(d->var_decl.name, d->line), ff->field.name, 0, d->line);
                    ASTNode *asg = ast_assign("=", lhs, elem, d->line);
                    initlist->block.stmts[flat] = NULL; /* ownership moves to the new assign stmt */
                    init_stmts_buf[n_init_stmts++] = ast_expr_stmt(asg, d->line);
                    flat++;
                }
                continue;
            }
            ASTNode *lhs = ast_var(d->var_decl.name, d->line);
            ASTNode *asg = ast_assign("=", lhs, d->var_decl.init, d->line);
            /* Transfer ownership of the init subtree to the new assign
             * statement — leaving it referenced from both here and the
             * original var_decl would double-free it when ast_free() walks
             * the whole program at the end of compilation. */
            d->var_decl.init = NULL;
            init_stmts_buf[n_init_stmts++] = ast_expr_stmt(asg, d->line);
        }
        if (n_init_stmts > 0) {
            int found_main = 0;
            for (int i=0;i<prog->program.count;i++) {
                ASTNode *d=prog->program.decls[i];
                if (d && d->kind==AST_FUNC_DECL && d->func.body &&
                    strcmp(d->func.name,"main")==0) {
                    ASTNode *body = d->func.body;
                    int old_count = body->block.count;
                    int new_count = old_count + n_init_stmts;
                    ASTNode **new_stmts = malloc(new_count * sizeof(ASTNode*));
                    memcpy(new_stmts, init_stmts_buf, n_init_stmts*sizeof(ASTNode*));
                    if (old_count>0)
                        memcpy(new_stmts+n_init_stmts, body->block.stmts, old_count*sizeof(ASTNode*));
                    free(body->block.stmts);
                    body->block.stmts = new_stmts;
                    body->block.count = new_count;
                    found_main = 1;
                    break;
                }
            }
            if (!found_main) {
                /* No "main" in this translation unit — this is a library
                 * object compiled separately with "squash -c" (see
                 * objfile.h/.c) and later linked against a driver file that
                 * DOES have main(). The global/static function-pointer
                 * initializers collected above (e.g. SDL_malloc.c's own
                 * "static struct {...} s_mem = { real_malloc, ... };") still
                 * need to run before anything in this object touches them —
                 * SDL_malloc() reading a permanently-zero s_mem.malloc_func
                 * and jumping through a null function pointer is exactly
                 * the RIP=0 crash this was root-caused to. Since there's no
                 * main() here to prepend into, synthesize a real exported
                 * function carrying the same statements; objfile_merge()
                 * collects every object's "__sqx_static_init" export and the
                 * final link stage (compiler.c) calls all of them before
                 * jumping to the real main(), the same way this file's own
                 * inits would have run first thing inside main(). */
                TypeInfo *voidret = typeinfo_new("void");
                voidret->pointer_depth = 0;
                ASTNode *initbody = ast_block(init_stmts_buf, n_init_stmts, 0);
                ASTNode *initfunc = ast_func_decl(NULL, voidret, "__sqx_static_init",
                    NULL, 0, 0, initbody, 0);
                int new_prog_count = prog->program.count + 1;
                ASTNode **new_decls = malloc((size_t)new_prog_count * sizeof(ASTNode*));
                memcpy(new_decls, prog->program.decls, (size_t)prog->program.count * sizeof(ASTNode*));
                new_decls[prog->program.count] = initfunc;
                free(prog->program.decls);
                prog->program.decls = new_decls;
                prog->program.count = new_prog_count;
            }
        }
        free(init_stmts_buf);
    }

    /* Pass 1: allocate labels for functions WITH bodies only.
     * Extern/forward declarations are handled via IAT (asm_reloc_iat).    */
    for (int i=0;i<prog->program.count;i++) {
        ASTNode *d=prog->program.decls[i];
        if (d->kind==AST_FUNC_DECL && d->func.body!=NULL)
            get_func_label(cg,d->func.name);
    }
    /* Pass 2: generate code for functions that have bodies.
     * Forward declarations (body==NULL) and typedef/struct/enum nodes are skipped. */
    for (int i=0;i<prog->program.count;i++) {
        ASTNode *d=prog->program.decls[i];
        if (!d) continue;
        if (d->kind==AST_FUNC_DECL && d->func.body!=NULL) {
            codegen_func(cg,d);
        }
        /* (Global VAR_DECLs handled in Pass 0 above) */
    }
    asm_resolve(cg->asm_);
}

uint8_t    *codegen_get_text  (CodeGen *cg, int *len) { *len=cg->asm_->code_len; return cg->asm_->code; }
uint8_t    *codegen_get_rdata (CodeGen *cg, int *len) { return build_rdata(cg,len); }
Relocation *codegen_get_relocs(CodeGen *cg, int *cnt) { *cnt=cg->asm_->reloc_count; return cg->asm_->relocs; }

/* =========================================================================
 * Float constant pool — stores double literals in .rdata for RIP-relative access
 * ========================================================================= */
const char *intern_float_const(CodeGen *cg, const char *val_bytes) {
    /* Store float constant directly in the string pool (.rdata) so that
     * RELOC_DATA_REL32 / RELOC_DATA_ABS32 can resolve it via string_labels[]. */
    char val_buf[8]; memcpy(val_buf, val_bytes, 8);
    /* Check if already interned — use memcmp (not uint64_t ==) so that the
     * 32-bit self-hosted compiler compares all 8 bytes, not just the low 4.
     * Example: -0.0 (0x8000000000000000) has low-32=0 same as 0.0 (0), so
     * a plain 32-bit == comparison would wrongly return the 0.0 label. */
    for (int i = 0; i < cg->string_count; i++) {
        if (cg->strings[i].len == 8 && memcmp(cg->strings[i].value, val_buf, 8) == 0)
            return cg->strings[i].label;
    }
    /* Allocate 8-byte entry in string pool */
    if (cg->string_count == cg->string_cap) {
        cg->string_cap *= 2;
        cg->strings = realloc(cg->strings, cg->string_cap * sizeof(StringEntry));
    }
    /* 8-byte aligned offset in pool */
    int off = (cg->string_pool_size + 7) & ~7;
    cg->string_pool_size = off + 8;

    char *buf = malloc(8); memcpy(buf, val_buf, 8);
    char *lbl = malloc(32);
    snprintf(lbl, 32, "fconst_%d", cg->string_count);

    StringEntry *se = &cg->strings[cg->string_count++];
    se->value   = buf;
    se->len     = 8;
    se->offset  = off;
    se->label   = lbl;
    se->is_wide = 0;
    return lbl;
}

/* =========================================================================
 * codegen_is_float_expr — determine if an AST node produces a float result
 * ========================================================================= */
/* Resolve typedef aliases when checking if a type is float */
static int is_float_type(CodeGen *cg, TypeInfo *t) {
    if (!t) return 0;
    if (typeinfo_is_float(t)) return 1;
    /* Try resolving typedef */
    if (t->base && t->pointer_depth==0) {
        Symbol *td = symtable_lookup(cg->sym, t->base);
        if (td && td->kind==SYM_TYPEDEF && td->type)
            return typeinfo_is_float(td->type);
    }
    return 0;
}

/* Get the effective (typedef-resolved) size of a type */
static int effective_typeinfo_size(CodeGen *cg, TypeInfo *t, int is_64bit) {
    if (!t) return 4;
    if (t->pointer_depth > 0) return typeinfo_size(t, is_64bit);
    int sz = typeinfo_size(t, is_64bit);
    if (sz != 4) return sz; /* known size, not the "unknown=4" default */
    /* Check if it's a typedef that resolves to something else */
    if (t->base) {
        Symbol *td = symtable_lookup(cg->sym, t->base);
        if (td && td->kind==SYM_TYPEDEF && td->type)
            return typeinfo_size(td->type, is_64bit);
    }
    return sz;
}

/* Resolve a struct/union field's TypeInfo* given the field's containing
 * object expression and field name — same struct-symbol lookup pattern
 * used inline by codegen_is_float_expr's AST_MEMBER case, but exposed so
 * array-of-struct-with-float-field element sizing can reuse it. */
static TypeInfo *field_type_of(CodeGen *cg, ASTNode *obj, const char *field_name) {
    if (!obj || !field_name) return NULL;
    const char *stype = NULL;
    if (obj->kind == AST_VAR) {
        Symbol *sv = symtable_lookup(cg->sym, obj->var.name);
        if (sv && sv->type) stype = sv->type->base;
    } else if (obj->kind == AST_DEREF) {
        ASTNode *op = obj->deref.operand;
        if (op->kind == AST_VAR) {
            Symbol *sv2 = symtable_lookup(cg->sym, op->var.name);
            if (sv2 && sv2->type) stype = sv2->type->base;
        }
    } else if (obj->kind == AST_MEMBER || obj->kind == AST_INDEX) {
        stype = resolve_node_type(cg->sym, obj);
    }
    if (!stype) return NULL;
    const char *bare = stype;
    if (strncmp(bare,"struct ",7)==0) bare+=7;
    else if (strncmp(bare,"union ",6)==0) bare+=6;
    char sk[256]; snprintf(sk,sizeof sk,"struct %s",bare);
    Symbol *ss = symtable_lookup(cg->sym, sk);
    if (!ss || !ss->struct_node) {
        /* bare tag may name a union, not a struct — see the identical
         * fallback (and full explanation) in resolve_node_type()'s
         * AST_MEMBER case just above this function. */
        snprintf(sk,sizeof sk,"union %s",bare);
        ss = symtable_lookup(cg->sym, sk);
    }
    if (!ss || !ss->struct_node) {
        /* "stype" may be a typedef NAME (e.g. "MyTable" from
         * "typedef struct { ... } MyTable;"), not a literal struct tag —
         * anonymous struct typedefs get an auto-generated tag ("$anonN")
         * distinct from the typedef name, so "struct MyTable" never
         * matches anything. Resolve one hop through the typedef to the
         * real tag and retry, same one-hop pattern used elsewhere in this
         * file (e.g. sizeof_type_sym). */
        Symbol *td = symtable_lookup(cg->sym, bare);
        if (td && td->kind==SYM_TYPEDEF && td->type && td->type->pointer_depth==0 && td->type->base) {
            const char *tb = td->type->base;
            const char *tbare = tb;
            if (strncmp(tbare,"struct ",7)==0) tbare+=7;
            else if (strncmp(tbare,"union ",6)==0) tbare+=6;
            char tk[256]; snprintf(tk,sizeof tk,"struct %s",tbare);
            Symbol *tss = symtable_lookup(cg->sym, tk);
            if (tss && tss->struct_node) ss = tss;
        }
    }
    if (ss && ss->struct_node) {
        for (int i=0;i<ss->struct_node->struct_decl.nfields;i++) {
            ASTNode *ff = ss->struct_node->struct_decl.fields[i];
            if (ff && ff->field.name && strcmp(ff->field.name,field_name)==0)
                return ff->field.type;
        }
    }
    return NULL;
}

/* An array-typed struct field's element count lives on the AST_FIELD node
 * itself (ff->field.array_size, set by ast_field() in the parser), NOT on
 * ff->field.type->array_size -- struct offset/size computation (see the
 * "Walk fields" loop above this function) deliberately relies on
 * ff->field.type->array_size staying unset for array fields: it sizes one
 * ELEMENT via sizeof_type_sym(f->field.type,...) and multiplies by
 * f->field.array_size itself afterward. So this must NOT be "fixed" by
 * patching ff->field.type->array_size in place (tried that first -- it
 * silently double-multiplied every array field's contribution to struct
 * size/offsets computed afterward, corrupting layout for any struct with
 * an array field, a much worse regression than the bug it fixed). Returns
 * 0 if `field_name` isn't found or isn't an array field. */
static int field_array_size_of(CodeGen *cg, ASTNode *obj, const char *field_name) {
    if (!obj || !field_name) return 0;
    const char *stype = NULL;
    if (obj->kind == AST_VAR) {
        Symbol *sv = symtable_lookup(cg->sym, obj->var.name);
        if (sv && sv->type) stype = sv->type->base;
    } else if (obj->kind == AST_DEREF) {
        ASTNode *op = obj->deref.operand;
        if (op->kind == AST_VAR) {
            Symbol *sv2 = symtable_lookup(cg->sym, op->var.name);
            if (sv2 && sv2->type) stype = sv2->type->base;
        }
    } else if (obj->kind == AST_MEMBER || obj->kind == AST_INDEX) {
        stype = resolve_node_type(cg->sym, obj);
    }
    if (!stype) return 0;
    const char *bare = stype;
    if (strncmp(bare,"struct ",7)==0) bare+=7;
    else if (strncmp(bare,"union ",6)==0) bare+=6;
    char sk[256]; snprintf(sk,sizeof sk,"struct %s",bare);
    Symbol *ss = symtable_lookup(cg->sym, sk);
    if (!ss || !ss->struct_node) {
        snprintf(sk,sizeof sk,"union %s",bare);
        ss = symtable_lookup(cg->sym, sk);
    }
    if (!ss || !ss->struct_node) {
        Symbol *td = symtable_lookup(cg->sym, bare);
        if (td && td->kind==SYM_TYPEDEF && td->type && td->type->pointer_depth==0 && td->type->base) {
            const char *tb = td->type->base;
            const char *tbare = tb;
            if (strncmp(tbare,"struct ",7)==0) tbare+=7;
            else if (strncmp(tbare,"union ",6)==0) tbare+=6;
            char tk[256]; snprintf(tk,sizeof tk,"struct %s",tbare);
            Symbol *tss = symtable_lookup(cg->sym, tk);
            if (tss && tss->struct_node) ss = tss;
        }
    }
    if (ss && ss->struct_node) {
        for (int i=0;i<ss->struct_node->struct_decl.nfields;i++) {
            ASTNode *ff = ss->struct_node->struct_decl.fields[i];
            if (ff && ff->field.name && strcmp(ff->field.name,field_name)==0)
                return ff->field.array_size > 0 ? ff->field.array_size : 0;
        }
    }
    return 0;
}

/* Byte width (4 or 8) of a float/double-valued expression — only meaningful
 * when codegen_is_float_expr(n) is already known true. codegen_float_expr()
 * always produces a double in XMM0/ST0 regardless of the source type, so
 * callers that need to place the value in a specific ABI slot (e.g. a
 * function-pointer call's XMM argument registers) need this to know
 * whether to narrow it with cvtsd2ss first. Defaults to 8 (double) when
 * the expression's real type can't be determined — same default C itself
 * uses for float literals and arithmetic in ambiguous contexts. */
static int float_expr_width(CodeGen *cg, ASTNode *n) {
    if (!n) return 8;
    switch (n->kind) {
    case AST_FLOAT:
        return n->fnum.is_single ? 4 : 8;
    case AST_CAST:
        return (n->cast.type && n->cast.type->base && !strcmp(n->cast.type->base,"float")) ? 4 : 8;
    case AST_VAR: {
        Symbol *s = symtable_lookup(cg->sym, n->var.name);
        if (!s || !s->type) return 8;
        TypeInfo *t = s->type;
        if (t->pointer_depth==0 && t->base) {
            Symbol *td = symtable_lookup(cg->sym, t->base);
            if (td && td->kind==SYM_TYPEDEF && td->type) t = td->type;
        }
        return (t->base && !strcmp(t->base,"float")) ? 4 : 8;
    }
    case AST_MEMBER: {
        TypeInfo *ft = field_type_of(cg, n->member.obj, n->member.field);
        return (ft && ft->base && !strcmp(ft->base,"float")) ? 4 : 8;
    }
    case AST_INDEX: {
        int esz = elem_size_of(cg, n->index.array);
        return esz==4 ? 4 : 8;
    }
    default: return 8;
    }
}

/* Is an lvalue TARGET (obj.field / obj->field / a[i] / *ptr) itself typed
 * float/double? Needed because codegen_is_float_expr() only tells you
 * whether an EXPRESSION's VALUE is float — for an assignment "target = rhs;"
 * where rhs is a plain integer literal/expression (e.g. "rect.x = 100;"),
 * codegen_is_float_expr(rhs) correctly says "not float" (100 alone is an
 * int), so without also checking the TARGET's own type, a plain int-literal
 * store into a float-typed struct field/array element/deref never took the
 * float path at all — it fell through to the generic integer store, which
 * writes the literal's raw integer BITS into the 4-byte float slot instead
 * of converting it, so "rect.x = 100;" stored 100 (0x00000064) instead of
 * 100.0f's real bit pattern (0x42C80000). This was the actual root cause of
 * SDL3's rendering appearing to draw nothing for filled rects: every
 * SDL_FRect field set via a plain integer literal (extremely common —
 * "rect.w = 200;") silently held garbage, so by the time SW_QueueFillRects
 * cast it (correctly) to int, a near-zero denormal float truncated to 0. */
static int lvalue_target_is_float(CodeGen *cg, ASTNode *lhs) {
    if (!lhs) return 0;
    if (lhs->kind == AST_MEMBER) {
        TypeInfo *ft = field_type_of(cg, lhs->member.obj, lhs->member.field);
        if (ft && ft->pointer_depth==0 && ft->base) {
            Symbol *ftd = symtable_lookup(cg->sym, ft->base);
            if (ftd && ftd->kind==SYM_TYPEDEF && ftd->type) ft = ftd->type;
        }
        return ft && typeinfo_is_float(ft);
    }
    if (lhs->kind == AST_INDEX) return index_elem_is_float(cg, lhs);
    if (lhs->kind == AST_DEREF) {
        ASTNode *op = lhs->deref.operand;
        TypeInfo *t = NULL;
        if (op && op->kind == AST_VAR) {
            Symbol *s = symtable_lookup(cg->sym, op->var.name);
            if (s) t = s->type;
        }
        if (!t || !t->base || t->pointer_depth != 1) return 0;
        Symbol *td = symtable_lookup(cg->sym, t->base);
        if (td && td->kind==SYM_TYPEDEF && td->type) t = td->type;
        return t->base && (strcmp(t->base,"float")==0 || strcmp(t->base,"double")==0);
    }
    return 0;
}

/* Byte width (4 or 8) of a float/double assignment TARGET — a[i], obj.field,
 * or *ptr — used to pick movss (float) vs movsd (double) when storing.
 * Defaults to 8 when the target can't be resolved, matching the previous
 * always-8-byte behavior so unresolvable cases don't regress. */
static int float_store_target_size(CodeGen *cg, ASTNode *lhs) {
    if (!lhs) return 8;
    if (lhs->kind == AST_INDEX) {
        int esz = elem_size_of(cg, lhs->index.array);
        return (esz==4) ? 4 : 8;
    }
    if (lhs->kind == AST_MEMBER) {
        TypeInfo *ft = field_type_of(cg, lhs->member.obj, lhs->member.field);
        if (ft) return effective_typeinfo_size(cg, ft, cg->is_64bit)==4 ? 4 : 8;
        return 8;
    }
    if (lhs->kind == AST_DEREF) {
        int esz = elem_size_of(cg, lhs->deref.operand);
        return (esz==4) ? 4 : 8;
    }
    return 8;
}

/* Is `array[index]`'s ELEMENT a float/double? (array itself may be a plain
 * array, a pointer, or a struct field of either.) Checked against the base
 * type name directly rather than typeinfo_is_float()/is_float_type(), since
 * those reject pointer_depth>0 — but a `float *fp` variable being indexed
 * (fp[i]) has pointer_depth==1 on the pointer itself while its ELEMENT is
 * still scalar float. pointer_depth>1 (e.g. float**) means the element is
 * itself a pointer, not float, so that's excluded. */
static int index_elem_is_float(CodeGen *cg, ASTNode *idxnode) {
    if (!idxnode || idxnode->kind != AST_INDEX) return 0;
    ASTNode *arrexpr = idxnode->index.array;
    if (!arrexpr) return 0;
    TypeInfo *t = NULL;
    if (arrexpr->kind == AST_VAR) {
        Symbol *s = symtable_lookup(cg->sym, arrexpr->var.name);
        if (s) t = s->type;
    } else if (arrexpr->kind == AST_MEMBER) {
        t = field_type_of(cg, arrexpr->member.obj, arrexpr->member.field);
    } else if (arrexpr->kind == AST_INDEX) {
        /* Nested index -- idxnode is "arr[x][u]", arrexpr is the OUTER
         * "arr[x]" itself. A 2D array's element type is the same scalar
         * base type regardless of how many dimensions have been indexed
         * through already, so resolve it from the INNERMOST array base
         * the same way elem_size_of()'s own "Nested index" case does.
         * Without this, `float arr[N][M]; ... x = arr[i][j] * y;` silently
         * treated arr[i][j]'s float bit pattern as a plain 32-bit INTEGER
         * (routing through codegen_expr's int path + cvtsi2sd instead of
         * codegen_float_expr's movss+cvtss2sd) -- e.g. a stored 1.0f (bit
         * pattern 0x3F800000) read back and used in arithmetic as the
         * integer 1065353216 converted to double, producing wildly wrong
         * results (confirmed via a minimal repro building a cosine lookup
         * table into a 2D float array for a JPEG IDCT: every table read
         * inside an expression came back astronomically wrong even though
         * the table's own stored bytes were verified correct in memory --
         * this is a READ-side bug only, storing INTO arr[i][j] already
         * worked correctly via a separate, already-nested-index-aware code
         * path in codegen_lvalue/elem_size_of). */
        ASTNode *base = arrexpr->index.array;
        if (base && base->kind == AST_VAR) {
            Symbol *s = symtable_lookup(cg->sym, base->var.name);
            if (s) t = s->type;
        } else if (base && base->kind == AST_MEMBER) {
            t = field_type_of(cg, base->member.obj, base->member.field);
        }
    }
    if (!t || !t->base || t->pointer_depth > 1) return 0;
    Symbol *td = symtable_lookup(cg->sym, t->base);
    if (td && td->kind==SYM_TYPEDEF && td->type) t = td->type;
    return t->base && (strcmp(t->base,"float")==0 || strcmp(t->base,"double")==0 ||
                        strcmp(t->base,"long double")==0);
}

int codegen_is_float_expr(CodeGen *cg, ASTNode *n) {
    if (!n) return 0;
    switch (n->kind) {
    case AST_FLOAT: return 1;
    case AST_NUMBER: return 0;
    case AST_VAR: {
        Symbol *s = symtable_lookup(cg->sym, n->var.name);
        /* An array variable used bare (not indexed) decays to its address —
         * an integer/pointer value — even when its element type is float,
         * so `is_float_type` on the element type alone would wrongly say
         * "float" here. Caught by glClearNamedFramebufferfv(fbo, GL_COLOR,
         * 0, clear) — passing a `float clear[4]` array pointer through a
         * function-pointer call that (correctly, post-fix) now honors
         * codegen_is_float_expr() to route float args into XMM regs: this
         * misclassified the array-decay pointer as a float value too. */
        if (s && s->array_size > 0) return 0;
        if (s && s->type) return is_float_type(cg, s->type);
        return 0;
    }
    case AST_CAST: return n->cast.type && is_float_type(cg, n->cast.type);
    case AST_BINARY: {
        const char *op = n->binary.op;
        /* Comparison ops always return int (0/1), even if operands are float */
        int is_cmp = (!strcmp(op,"==")||!strcmp(op,"!=")||!strcmp(op,"<")||
                     !strcmp(op,"<=")||!strcmp(op,">")||!strcmp(op,">="));
        if (is_cmp) return 0;
        /* For arithmetic ops: if either operand is float, result is float */
        return codegen_is_float_expr(cg, n->binary.left) ||
               codegen_is_float_expr(cg, n->binary.right);
    }
    case AST_CALL: {
        /* Check return type of called function */
        /* Hard-coded float-returning shims */
        if (strcmp(n->call.name,"atof")==0) return 1;
        Symbol *s = symtable_lookup(cg->sym, n->call.name);
        if (s && s->type) return is_float_type(cg, s->type);
        return 0;
    }
    case AST_MEMBER: {
        /* Look up field type to check if it is float/double */
        ASTNode *mobj = n->member.obj;
        const char *mfname = n->member.field;
        const char *mstype = NULL;
        if (mobj->kind == AST_VAR) {
            Symbol *sv = symtable_lookup(cg->sym, mobj->var.name);
            if (sv && sv->type) mstype = sv->type->base;
        } else if (mobj->kind == AST_DEREF || n->member.arrow) {
            ASTNode *op = (mobj->kind==AST_DEREF) ? mobj->deref.operand : mobj;
            if (op->kind == AST_VAR) {
                Symbol *sv2 = symtable_lookup(cg->sym, op->var.name);
                if (sv2 && sv2->type) mstype = sv2->type->base;
            }
        } else if (mobj->kind == AST_MEMBER || mobj->kind == AST_INDEX) {
            mstype = resolve_node_type(cg->sym, mobj);
        }
        if (mstype) {
            const char *bare = mstype;
            if (strncmp(bare,"struct ",7)==0) bare+=7;
            else if (strncmp(bare,"union ",6)==0) bare+=6;
            char sk[256]; snprintf(sk,sizeof(sk),"struct %s",bare);
            Symbol *ss = symtable_lookup(cg->sym, sk);
            if (!ss) {
                /* typedef resolution: e.g. "typedef struct {...} Box;" then
                 * "Box *p" — sv->type->base for a POINTER-typed local stays
                 * the typedef name "Box" (unlike a direct "Box b;" local,
                 * whose base is already resolved to the real "$anonN" tag by
                 * the time it reaches here), so "struct Box" isn't found
                 * directly. Same root cause as the identical fallback
                 * already applied to the AST_MEMBER LOAD path above (see its
                 * comment) — without this, is_float_expr() returns 0 for
                 * ANY float/double field read through a typedef'd pointer's
                 * "->", so the caller falls back to plain integer codegen
                 * (a 32-bit "mov eax,[reg+off]") instead of the correct
                 * movss/cvtss2sd float load: confirmed via a minimal repro,
                 * "p->x" (float field) printed 0.0 via "%f" and the field's
                 * raw IEEE-754 bit pattern via "(int)p->x", while the
                 * identical "b.x" on a non-pointer local worked correctly. */
                Symbol *td = symtable_lookup(cg->sym, mstype);
                if (td && td->kind==SYM_TYPEDEF && td->type) {
                    const char *tb = td->type->base;
                    if (strncmp(tb,"struct ",7)==0) tb+=7;
                    else if (strncmp(tb,"union ",6)==0) tb+=6;
                    snprintf(sk,sizeof sk,"struct %s",tb);
                    ss = symtable_lookup(cg->sym, sk);
                }
            }
            if (ss && ss->struct_node) {
                for (int _i=0;_i<ss->struct_node->struct_decl.nfields;_i++) {
                    ASTNode *ff=ss->struct_node->struct_decl.fields[_i];
                    if (ff&&ff->field.name&&strcmp(ff->field.name,mfname)==0&&ff->field.type)
                        return is_float_type(cg, ff->field.type);
                }
            }
        }
        return 0;
    }
    case AST_ASSIGN: return codegen_is_float_expr(cg, n->assign.rhs);
    case AST_UNARY:  return codegen_is_float_expr(cg, n->unary.operand);
    case AST_INDEX:  return index_elem_is_float(cg, n);
    case AST_TERNARY:
        return codegen_is_float_expr(cg, n->ternary.then_) ||
               codegen_is_float_expr(cg, n->ternary.else_);
    case AST_DEREF: {
        /* "*ptr" reading a float/double through a pointer -- no case here
         * at all previously (fell to the `default: return 0` below), so
         * ANY dereferenced-pointer float READ (as opposed to a write,
         * which lvalue_target_is_float's own near-identical AST_DEREF
         * case already covered) was silently treated as an integer
         * expression everywhere it appeared: as a comparison operand
         * (codegen_branch's own float detection calls this same function),
         * as a plain value, inside arithmetic, as a call argument. Same
         * lookup shape as lvalue_target_is_float's AST_DEREF case (only a
         * bare "*ptrVar" — the common case -- not "*(p+1)"/"*p++"-style
         * pointer expressions). Confirmed via a minimal repro matching
         * real SDL3's own ConstrainMousePosition(SDL_Mouse*, SDL_Window*,
         * float *x, float *y) -- unmodified real SDL3 source, never
         * hand-patched -- whose "if (*x >= (float)(x_max+1))" style
         * comparisons silently misfired (comparing the float's raw bit
         * pattern as if it were a huge integer), and whose "*x = ..."
         * writes downstream then read back stale/wrong values in the
         * caller: the actual root cause of SDL_SendMouseMotion() never
         * updating SDL_Mouse's tracked cursor position for a REAL X11
         * MotionNotify event (confirmed reaching this exact function with
         * correct coordinates), which in turn made every subsequent real
         * mouse click report a stale/wrong position (SDL_SendMouseButton
         * reads its own event x/y from that same tracked position) --
         * genuine real clicks/hover silently landing on the wrong element
         * or nothing at all, while synthetic SDL_PushEvent-injected
         * testing (which never goes through ConstrainMousePosition at
         * all) never exercised this path. */
        ASTNode *op = n->deref.operand;
        TypeInfo *t = NULL;
        if (op && op->kind == AST_VAR) {
            Symbol *s = symtable_lookup(cg->sym, op->var.name);
            if (s) t = s->type;
        }
        if (!t || !t->base || t->pointer_depth != 1) return 0;
        Symbol *td = symtable_lookup(cg->sym, t->base);
        if (td && td->kind==SYM_TYPEDEF && td->type) t = td->type;
        return t->base && (strcmp(t->base,"float")==0 || strcmp(t->base,"double")==0);
    }
    default: return 0;
    }
}

/* =========================================================================
 * codegen_float_expr — evaluate float/double expression into XMM0 (64-bit)
 *                      or ST0 on the x87 stack (32-bit)
 * ========================================================================= */
void codegen_float_expr(CodeGen *cg, ASTNode *n) {
    Assembler *a = cg->asm_;
    if (!n) { /* load 0.0 */
        if (cg->is_64bit) { asm_xorpd(a,0,0); }
        else { asm_fldz(a); }
        return;
    }
    switch (n->kind) {

    case AST_FLOAT: {
        const char *lbl = intern_float_const(cg, (const char *)&n->fnum.value);
        if (cg->is_64bit) {
            asm_movsd_rip(a, 0, lbl);  /* movsd xmm0,[rip+fconst] */
        } else {
            /* fldl [rel32] — using EBX as scratch for 32-bit */
            asm_emit2(a,0xDD,0x05); asm_reloc_data(a,lbl); /* fldl [abs32] */
        }
        break;
    }

    case AST_NUMBER: {
        /* Integer literal coerced to float */
        double v = (double)n->num.value;
        const char *lbl = intern_float_const(cg, (const char *)&v);
        if (cg->is_64bit) {
            asm_movsd_rip(a, 0, lbl);
        } else {
            asm_emit2(a,0xDD,0x05); asm_reloc_data(a,lbl); /* fldl [abs32] */
        }
        break;
    }

    case AST_VAR: {
        Symbol *sv = symtable_lookup(cg->sym, n->var.name);
        if (!sv) { if(cg->is_64bit) asm_xorpd(a,0,0); else asm_fldz(a); return; }
        int fsz = sv->type ? typeinfo_size(sv->type, cg->is_64bit) : 8;
        if (sv->kind==SYM_VAR||sv->kind==SYM_PARAM) {
            if (cg->is_64bit) {
                if (fsz==4){asm_movss_load(a,0,REG_RBP,sv->offset);asm_cvtss2sd(a,0,0);}
                else asm_movsd_load(a,0,REG_RBP,sv->offset);
            } else {
                asm_fld_mem64(a,REG_EBP,sv->offset);
            }
        } else if (sv->kind==SYM_GLOBAL) {
            const char *lbl=(sv->dll&&sv->dll[0])?sv->dll:n->var.name;
            if (cg->is_64bit) {
                asm_lea_rip_wdata(a,REG_RBX,lbl);
                if (fsz==4){asm_movss_load(a,0,REG_RBX,0);asm_cvtss2sd(a,0,0);}
                else asm_movsd_load(a,0,REG_RBX,0);
            } else {
                asm_emit2(a,0xDD,0x05); asm_reloc_wdata(a,lbl); /* fldl [abs32] global */
            }
        } else {
            if(cg->is_64bit) asm_xorpd(a,0,0); else asm_fldz(a);
        }
        break;
    }

    case AST_BINARY: {
        const char *op = n->binary.op;
        int is_cmp = (!strcmp(op,"==")||!strcmp(op,"!=")||!strcmp(op,"<")||
                      !strcmp(op,"<=")||!strcmp(op,">")||!strcmp(op,">="));
        if (cg->is_64bit) {
            /* eval lhs->XMM0, spill to stack; eval rhs->XMM0/int, move to XMM1; pop lhs->XMM0 */
            /* If operand is not float, evaluate as int then convert */
            if (codegen_is_float_expr(cg, n->binary.left))
                codegen_float_expr(cg, n->binary.left);
            else {
                codegen_expr(cg, n->binary.left);
                asm_cvtsi2sd(a, 0, REG_RAX); /* int->double */
            }
            asm_sub_rsp(a,16);
            asm_emit4(a,0xF2,0x0F,0x11,0x04); asm_emit1(a,0x24); /* movsd [rsp],xmm0 */
            if (codegen_is_float_expr(cg, n->binary.right))
                codegen_float_expr(cg, n->binary.right);
            else {
                codegen_expr(cg, n->binary.right);
                asm_cvtsi2sd(a, 0, REG_RAX); /* int->double */
            }
            asm_movsd_xmm(a,1,0);                    /* XMM1 = rhs */
            asm_emit4(a,0xF2,0x0F,0x10,0x04); asm_emit1(a,0x24); /* movsd xmm0,[rsp] = lhs */
            asm_add_rsp(a,16);                       /* XMM0=lhs, XMM1=rhs */
            if      (!strcmp(op,"+")) asm_addsd(a,0,1);
            else if (!strcmp(op,"-")) asm_subsd(a,0,1);
            else if (!strcmp(op,"*")) asm_mulsd(a,0,1);
            else if (!strcmp(op,"/")) asm_divsd(a,0,1);
            else if (!strcmp(op,"^")) asm_xorpd(a,0,1);
            else if (is_cmp) {
                asm_ucomisd(a,0,1);
                asm_mov_reg_imm(a,REG_RAX,0);
                if      (!strcmp(op,"==")) {asm_emit3(a,0x0F,0x94,0xC0);}
                else if (!strcmp(op,"!=")) {asm_emit3(a,0x0F,0x95,0xC0);}
                else if (!strcmp(op,"<"))  {asm_emit3(a,0x0F,0x92,0xC0);}
                else if (!strcmp(op,"<=")) {asm_emit3(a,0x0F,0x96,0xC0);}
                else if (!strcmp(op,">"))  {asm_emit3(a,0x0F,0x97,0xC0);}
                else if (!strcmp(op,">=")) {asm_emit3(a,0x0F,0x93,0xC0);}
                asm_emit3(a,0x83,0xE0,0x01); /* and eax,1 - zero-extends to 64-bit */
                return;
            }
        } else {
            /* 32-bit x87: push lhs (ST0), then push rhs (new ST0, old lhs=ST1) */
            if (codegen_is_float_expr(cg, n->binary.left))
                codegen_float_expr(cg, n->binary.left);
            else {
                codegen_expr(cg, n->binary.left);
                /* int->float on x87: push int to stack, load with fild */
                asm_sub_rsp(a,4); asm_mov_mem_reg(a,REG_ESP,0,REG_EAX);
                asm_fild_mem32(a,REG_ESP,0); asm_add_rsp(a,4);
            }
            if (codegen_is_float_expr(cg, n->binary.right))
                codegen_float_expr(cg, n->binary.right);
            else {
                codegen_expr(cg, n->binary.right);
                asm_sub_rsp(a,4); asm_mov_mem_reg(a,REG_ESP,0,REG_EAX);
                asm_fild_mem32(a,REG_ESP,0); asm_add_rsp(a,4);
            }
            if      (!strcmp(op,"+")) asm_faddp(a);
            else if (!strcmp(op,"-")) asm_fsubp(a);  /* ST1=ST1-ST0=lhs-rhs */
            else if (!strcmp(op,"*")) asm_fmulp(a);
            else if (!strcmp(op,"/")) asm_fdivp(a);  /* ST1=ST1/ST0=lhs/rhs */
            else if (is_cmp) {
                /* fcompp: compares ST0(rhs) with ST1(lhs), pops twice.
                 * CF=1 if ST0<ST1(rhs<lhs), ZF=1 if equal, CF=0,ZF=0 if ST0>ST1 */
                asm_fcompp(a); asm_fnstsw(a); asm_sahf(a);
                asm_mov_reg_imm(a,REG_EAX,0);
                if      (!strcmp(op,"==")) {asm_emit3(a,0x0F,0x94,0xC0);} /* sete  ZF=1 */
                else if (!strcmp(op,"!=")) {asm_emit3(a,0x0F,0x95,0xC0);} /* setne ZF=0 */
                else if (!strcmp(op,">"))  {asm_emit3(a,0x0F,0x92,0xC0);} /* setb  CF=1 (rhs<lhs = lhs>rhs) */
                else if (!strcmp(op,">=")) {asm_emit3(a,0x0F,0x96,0xC0);} /* setbe CF=1|ZF=1 */
                else if (!strcmp(op,"<"))  {asm_emit3(a,0x0F,0x97,0xC0);} /* seta  CF=0,ZF=0 (rhs>lhs = lhs<rhs) */
                else if (!strcmp(op,"<=")) {asm_emit3(a,0x0F,0x93,0xC0);} /* setae CF=0 */
                asm_emit3(a,0x83,0xE0,0x01); /* and eax,1 */ return;
            }
        }
        break;
    }

    case AST_UNARY: {
        const char *op = n->unary.op;
        codegen_float_expr(cg, n->unary.operand);
        if (strcmp(op,"-")==0) {
            if (cg->is_64bit) {
                /* XOR with sign bit: xorpd xmm0,[neg_mask] */
                double neg = -0.0;
                const char *nlbl = intern_float_const(cg, (const char *)&neg);
                /* Load neg mask into xmm1, xor */
                asm_emit1(a,0xF2); asm_emit2(a,0x0F,0x10);
                asm_emit1(a,0x0D); asm_reloc_data(a,nlbl); /* movsd xmm1,[rip+neg_mask] */
                asm_xorpd(a,0,1);
            } else { asm_fchs(a); }
        }
        break;
    }

    case AST_CAST: {
        TypeInfo *t = n->cast.type;
        if (codegen_is_float_expr(cg, n->cast.expr)) {
            codegen_float_expr(cg, n->cast.expr);
            /* codegen_float_expr always produces double in XMM0.
             * Only convert if the cast target is single-precision float. */
            if (t && strcmp(t->base,"float")==0 && cg->is_64bit) asm_cvtsd2ss(a,0,0);
            /* (double) cast: already double in XMM0 — no conversion needed */
        } else {
            /* int -> float */
            codegen_expr(cg, n->cast.expr);
            if (cg->is_64bit) {
                asm_cvtsi2sd(a, 0, REG_RAX);
                if (t && strcmp(t->base,"float")==0) asm_cvtsd2ss(a,0,0);
            } else {
                /* Store int to stack, use fild */
                asm_sub_rsp(a,4); asm_mov_mem_reg(a,REG_ESP,0,REG_EAX);
                asm_fild_mem32(a,REG_ESP,0); asm_add_rsp(a,4);
            }
        }
        break;
    }

    case AST_ASSIGN: {
        /* Evaluate rhs into xmm0/ST0, then store */
        codegen_float_expr(cg, n->assign.rhs);
        /* Store back: get lvalue address */
        ASTNode *lhs = n->assign.lhs;
        if (lhs->kind == AST_VAR) {
            Symbol *s = symtable_lookup(cg->sym, lhs->var.name);
            if (s && (s->kind==SYM_VAR||s->kind==SYM_PARAM)) {
                if (cg->is_64bit) {
                    int sz=s->type?typeinfo_size(s->type,1):8;
                    if (sz==4) { asm_cvtsd2ss(a,0,0); asm_movss_store(a,REG_RBP,s->offset,0); }
                    else asm_movsd_store(a,REG_RBP,s->offset,0);
                } else {
                    asm_fstp_mem64(a,REG_RBP,s->offset);
                    asm_fld_mem64(a,REG_RBP,s->offset); /* reload for expression value */
                }
            } else if (s && s->kind==SYM_GLOBAL) {
                const char *lbl=(s->dll&&s->dll[0])?s->dll:lhs->var.name;
                if (cg->is_64bit) {
                    /* Match the SYM_VAR/SYM_PARAM branch just above: a
                     * "float" (4-byte) global must be narrowed and stored
                     * with movss, not always written as an 8-byte movsd —
                     * the global's own storage is only 4 bytes for a float
                     * (see wdata allocation), so an unconditional 8-byte
                     * store here corrupted the next 4 bytes of .data AND
                     * left this global unreadable as a float (a later
                     * float-sized read via movss saw the double bit
                     * pattern's low 4 bytes, which are exactly zero for
                     * any "round" value — confirmed via a standalone
                     * repro: "float g; void f(float h){g=h;}" left g
                     * reading back as 0.0 for every non-fractional h). */
                    int sz=s->type?typeinfo_size(s->type,1):8;
                    asm_lea_rip_wdata(a,REG_RBX,lbl);
                    if (sz==4) { asm_cvtsd2ss(a,0,0); asm_movss_store(a,REG_RBX,0,0); }
                    else asm_movsd_store(a,REG_RBX,0,0);
                } else {
                    /* 32-bit: fstp to global wdata address, then reload */
                    asm_emit2(a,0xDD,0x1D); asm_reloc_wdata(a,lbl); /* fstp qword [lbl] */
                    asm_emit2(a,0xDD,0x05); asm_reloc_wdata(a,lbl); /* fld  qword [lbl] */
                }
            }
        }
        break;
    }

    case AST_CALL: {
        /* Call float-returning function: result lands in XMM0 (64-bit)
         * or ST0 (32-bit x87). Use codegen_expr which already handles this. */
        codegen_expr(cg, n);
        break;
    }

    case AST_TERNARY: {
        /* cond ? float_then : float_else — evaluate each branch as float */
        int else_lbl = asm_new_label(a, "ftern_else");
        int end_lbl  = asm_new_label(a, "ftern_end");
        codegen_expr(cg, n->ternary.cond);
        asm_test_reg_reg(a, REG_RAX, REG_RAX);
        asm_jcc_label(a, CC_E, else_lbl);
        /* then branch */
        if (codegen_is_float_expr(cg, n->ternary.then_))
            codegen_float_expr(cg, n->ternary.then_);
        else { codegen_expr(cg, n->ternary.then_); asm_cvtsi2sd(a, 0, REG_RAX); }
        asm_jmp_label(a, end_lbl);
        asm_def_label(a, else_lbl);
        /* else branch */
        if (codegen_is_float_expr(cg, n->ternary.else_))
            codegen_float_expr(cg, n->ternary.else_);
        else { codegen_expr(cg, n->ternary.else_); asm_cvtsi2sd(a, 0, REG_RAX); }
        asm_def_label(a, end_lbl);
        break;
    }

    case AST_MEMBER: {
        /* Float/double struct field: get base address, then load. Width must
         * match the field's actual declared size (float=4, double=8) — always
         * reading/writing 8 bytes corrupts an adjacent field for `float`
         * members (movsd spans past a 4-byte float into whatever follows). */
        if (n->member.arrow) codegen_expr(cg, n->member.obj);
        else codegen_lvalue(cg, n->member.obj);
        int foff = field_byte_offset(cg->sym, n->member.obj, n->member.field);
        TypeInfo *fty = field_type_of(cg, n->member.obj, n->member.field);
        int fsz = fty ? effective_typeinfo_size(cg, fty, cg->is_64bit) : 8;
        if (cg->is_64bit) {
            if (fsz==4) { asm_movss_load(a,0,REG_RAX,foff); asm_cvtss2sd(a,0,0); }
            else asm_movsd_load(a, 0, REG_RAX, foff);
        }
        else asm_fld_mem64(a, REG_EAX, foff);
        break;
    }

    case AST_INDEX: {
        /* Float/double array element: compute the element's address, then
         * load with the correct width (see AST_MEMBER above for why width
         * matters). codegen_lvalue only touches integer registers. */
        codegen_lvalue(cg, n);
        int esz = elem_size_of(cg, n->index.array);
        if (cg->is_64bit) {
            if (esz==4) { asm_movss_load(a,0,REG_RAX,0); asm_cvtss2sd(a,0,0); }
            else asm_movsd_load(a, 0, REG_RAX, 0);
        } else asm_fld_mem64(a, REG_EAX, 0);
        break;
    }

    case AST_DEREF: {
        /* "*ptr" reading a float/double through a pointer -- see
         * codegen_is_float_expr()'s own AST_DEREF case (its own long
         * comment has the full story) for why this case's ABSENCE was the
         * real bug: without it, this whole switch never even ran for a
         * float-pointee dereference (codegen_is_float_expr said "not
         * float" so codegen_expr's own top-level float-dispatch check
         * never routed here in the first place) -- and even on the rare
         * caller that invokes codegen_float_expr() directly regardless
         * (e.g. an explicit float-context load), this fell to the
         * `default:` case just below, which loads the pointee's raw bytes
         * as an INTEGER and numerically CONVERTS that integer to a double
         * (cvtsi2sd) instead of reinterpreting the same bytes as an
         * IEEE-754 float/double bit pattern (movss+cvtss2sd / movsd) --
         * e.g. dereferencing a float holding exactly 5.0f (bit pattern
         * 0x40A00000, decimal 1084227584) came back as 1084227584.0
         * instead of 5.0. Same pointee-type resolution (bare "*ptrVar"
         * only) as codegen_is_float_expr's own case, and the same
         * single/double width selection AST_INDEX just above already
         * uses. */
        ASTNode *op = n->deref.operand;
        TypeInfo *t = NULL;
        if (op && op->kind == AST_VAR) {
            Symbol *s = symtable_lookup(cg->sym, op->var.name);
            if (s) t = s->type;
        }
        Symbol *td = (t && t->base) ? symtable_lookup(cg->sym, t->base) : NULL;
        TypeInfo *pt = (td && td->kind==SYM_TYPEDEF && td->type) ? td->type : t;
        int is_single = pt && pt->base && strcmp(pt->base,"float")==0;
        codegen_expr(cg, op); /* pointer value -> RAX */
        if (cg->is_64bit) {
            if (is_single) { asm_movss_load(a,0,REG_RAX,0); asm_cvtss2sd(a,0,0); }
            else asm_movsd_load(a, 0, REG_RAX, 0);
        } else asm_fld_mem64(a, REG_EAX, 0);
        break;
    }

    default:
        /* Safe fallback: evaluate as integer and convert to double */
        codegen_expr(cg, n);
        if (cg->is_64bit) { asm_cvtsi2sd(a, 0, REG_RAX); }
        else {
            asm_sub_rsp(a, 4); asm_mov_mem_reg(a, REG_ESP, 0, REG_EAX);
            asm_fild_mem32(a, REG_ESP, 0); asm_add_rsp(a, 4);
        }
        break;
    }
}

/* Accessor for wdata pool from compiler.c */
uint8_t *codegen_get_wdata(CodeGen *cg, int *len) {
    *len = cg->wdata_pool_size;
    uint8_t *buf = calloc(cg->wdata_pool_size+1,1);
    for (int i=0;i<cg->wdata_count;i++) {
        WDataEntry *we = &cg->wdata[i];
        if (we->content) memcpy(buf+we->offset, we->content, (size_t)we->size);
    }
    return buf;
}
char **codegen_get_wdata_labels(CodeGen *cg, int *count) {
    *count = cg->wdata_count;
    char **arr = malloc(cg->wdata_count * sizeof(char*));
    for (int i=0;i<cg->wdata_count;i++) arr[i]=cg->wdata[i].label;
    return arr;
}
int *codegen_get_wdata_offsets(CodeGen *cg, int *count) {
    *count = cg->wdata_count;
    int *arr = malloc(cg->wdata_count * sizeof(int));
    for (int i=0;i<cg->wdata_count;i++) arr[i]=cg->wdata[i].offset;
    return arr;
}/* Check if expression subtree contains any function call */
static int expr_has_call(ASTNode *n) {
    if (!n) return 0;
    /* AST_FUNC_PTR_CALL (calling through a function-pointer EXPRESSION —
     * e.g. "iface.fn(x)" or "obj->fn(x)", as opposed to AST_CALL's plain
     * "named_function(x)") was missing here entirely, so an argument like
     * "printf(fmt, iface.write_file(a,b,c))" was reported as "doesn't
     * contain a call" — the printf/snprintf/fprintf arg-marshalling shims
     * above only protect already-placed argument registers (RCX/RDX/R8/R9)
     * when expr_has_call() says an argument's evaluation might clobber
     * them. Since the fn-ptr call's own argument setup DOES clobber those
     * same volatile registers, the previously-placed argument(s) were
     * silently corrupted the moment any argument was itself a call
     * through a function pointer (a struct/interface function-pointer
     * field is an extremely common case for this). */
    if (n->kind == AST_CALL || n->kind == AST_FUNC_PTR_CALL) return 1;
    switch(n->kind) {
        case AST_BINARY: {
            /* <<, >>, /, % all use RCX as a scratch register during codegen,
             * so they clobber any arg already placed in RCX/RDX. */
            const char *op = n->binary.op;
            if (op && (strcmp(op,"<<")==0 || strcmp(op,">>")==0 ||
                       strcmp(op,"/")==0  || strcmp(op,"%")==0))
                return 1;
            return expr_has_call(n->binary.left) || expr_has_call(n->binary.right);
        }
        case AST_UNARY:   return expr_has_call(n->unary.operand);
        case AST_ASSIGN:  return expr_has_call(n->assign.rhs);
        case AST_CAST:    return expr_has_call(n->cast.expr);
        case AST_TERNARY: return expr_has_call(n->ternary.cond)||expr_has_call(n->ternary.then_)||expr_has_call(n->ternary.else_);
        case AST_INDEX:   return expr_has_call(n->index.array)||expr_has_call(n->index.index);
        case AST_MEMBER:  return expr_has_call(n->member.obj);
        default: return 0;
    }
}

/* Emit a conditional branch: jump to lbl if condition is false (jump_if_false=1)
 * or true (jump_if_false=0). Avoids SETCC for simple comparisons. */
static void codegen_branch(CodeGen *cg, ASTNode *cond, int lbl, int jump_if_true) {
    Assembler *a = cg->asm_;
    if (!cond) { if (!jump_if_true) asm_jmp_label(a,lbl); return; }
    /* For comparison binary ops: emit CMP + Jcc directly (no SETCC needed) */
    if (cond->kind == AST_BINARY) {
        const char *op = cond->binary.op;
        int is_cmp = (!strcmp(op,"==")||!strcmp(op,"!=")||!strcmp(op,"<")||
                      !strcmp(op,"<=")||!strcmp(op,">")||!strcmp(op,">="));
        if (is_cmp) {
            /* Float-operand comparisons used directly as an if/while
             * condition (e.g. "if (gain != 1.0f)") MUST take a completely
             * different path — the integer CMP+Jcc code below reads both
             * operands via plain codegen_expr() and compares raw register
             * bits as if they were integers, but codegen_expr() on a bare
             * float AST_VAR does not reliably produce the operand's float
             * bit pattern in RAX (that's what codegen_float_expr()'s XMM
             * path is for). Confirmed via a minimal repro matching real
             * SDL3's SDL_audiocvt.c ConvertAudio(): "if (gain != 1.0f)"
             * with gain correctly holding 1.0f's exact bits still took the
             * true branch, entering a gain-scaling loop that wrote past an
             * undersized/wrong buffer — a real STATUS_ACCESS_VIOLATION.
             * Every OTHER float-comparison call site in this file (plain
             * value comparisons via codegen_expr's own AST_BINARY case,
             * and codegen_float_expr's own is_cmp branch) already detects
             * float operands via codegen_is_float_expr() first; this
             * "fast path for if/while conditions" was the one comparison
             * codegen site that never did, silently miscompiling any
             * float comparison used directly as a branch condition. */
            int lhs_is_float = codegen_is_float_expr(cg, cond->binary.left);
            int rhs_is_float = codegen_is_float_expr(cg, cond->binary.right);
            if (cg->is_64bit && (lhs_is_float || rhs_is_float)) {
                if (lhs_is_float) codegen_float_expr(cg, cond->binary.left);
                else { codegen_expr(cg, cond->binary.left); asm_cvtsi2sd(a, 0, REG_RAX); }
                asm_sub_rsp(a,16);
                asm_emit4(a,0xF2,0x0F,0x11,0x04); asm_emit1(a,0x24); /* movsd [rsp],xmm0 */
                if (rhs_is_float) codegen_float_expr(cg, cond->binary.right);
                else { codegen_expr(cg, cond->binary.right); asm_cvtsi2sd(a, 0, REG_RAX); }
                asm_movsd_xmm(a,1,0);                                /* xmm1 = rhs */
                asm_emit4(a,0xF2,0x0F,0x10,0x04); asm_emit1(a,0x24); /* movsd xmm0,[rsp] = lhs */
                asm_add_rsp(a,16);
                asm_ucomisd(a,0,1);
                /* UCOMISD sets flags exactly like an UNSIGNED integer
                 * compare (CF/ZF/PF), so the existing unsigned condition
                 * codes apply directly — same codes codegen_float_expr's
                 * own is_cmp branch uses for its SETcc byte (0F 9x), just
                 * as a Jcc (0F 8x) here instead. */
                CondCode fcc;
                if      (!strcmp(op,"==")) fcc = jump_if_true ? CC_E  : CC_NE;
                else if (!strcmp(op,"!=")) fcc = jump_if_true ? CC_NE : CC_E;
                else if (!strcmp(op,"<"))  fcc = jump_if_true ? CC_B  : CC_AE;
                else if (!strcmp(op,"<=")) fcc = jump_if_true ? CC_BE : CC_A;
                else if (!strcmp(op,">"))  fcc = jump_if_true ? CC_A  : CC_BE;
                else                       fcc = jump_if_true ? CC_AE : CC_B;
                asm_jcc_label(a, fcc, lbl);
                return;
            }
            /* Evaluate both sides, CMP, then Jcc.
             * 64-bit: PUSH/POP to save left operand (red zone [RSP-8] is
             *         unsafe on Windows - OS can overwrite memory below RSP).
             * 32-bit: cdecl only requires 4-byte alignment, PUSH/POP is fine. */
            codegen_expr(cg, cond->binary.left);
            /* Save left operand with PUSH (not [RSP-8] red zone - unsafe on Windows) */
            if (cg->is_64bit) {
                asm_push_reg(a, REG_RAX);
                codegen_expr(cg, cond->binary.right);
                asm_pop_reg(a, REG_RBX);
                asm_emit3(a,0x48,0x39,0xC3);  /* cmp rbx,rax */
            } else {
                asm_push_reg(a, REG_EAX);
                codegen_expr(cg, cond->binary.right);
                asm_emit1(a,0x5B);             /* pop ebx */
                asm_emit2(a,0x39,0xC3);        /* cmp ebx,eax */
            }
            /* Determine Jcc: jump to lbl when condition is FALSE (for if/while).
             * Same signed-vs-unsigned condition-code selection as the
             * AST_BINARY comparison path in codegen_expr (see its comment)
             * — this is a SEPARATE codegen path (branch conditions compile
             * straight to CMP+Jcc, skipping SETCC), so needed the identical
             * fix independently. This is the path that actually broke
             * "while (value > 0) { ...; value /= radix; }"-style loops in
             * SDL_ulltoa()/SDL_lltoa() for unsigned 64-bit values with the
             * high bit set: the loop condition itself (not just a value
             * comparison) evaluated wrong, so the loop body never ran at
             * all, silently producing an empty digit string. */
            int uns = strcmp(op,"==")!=0 && strcmp(op,"!=")!=0 &&
                      (expr_is_unsigned_int(cg,cond->binary.left) || expr_is_unsigned_int(cg,cond->binary.right));
            CondCode cc;
            if      (!strcmp(op,"==")) cc = jump_if_true ? CC_E  : CC_NE;
            else if (!strcmp(op,"!=")) cc = jump_if_true ? CC_NE : CC_E;
            else if (!strcmp(op,"<"))  cc = jump_if_true ? (uns?CC_B :CC_L)  : (uns?CC_AE:CC_GE);
            else if (!strcmp(op,"<=")) cc = jump_if_true ? (uns?CC_BE:CC_LE) : (uns?CC_A :CC_G);
            else if (!strcmp(op,">"))  cc = jump_if_true ? (uns?CC_A :CC_G)  : (uns?CC_BE:CC_LE);
            else                       cc = jump_if_true ? (uns?CC_AE:CC_GE) : (uns?CC_B :CC_L);
            asm_jcc_label(a, cc, lbl);
            return;
        }
    }
    /* Fallback: evaluate expression, test, and branch */
    codegen_expr(cg, cond);
    asm_test_reg_reg(a, cg->is_64bit ? REG_RAX : REG_EAX,
                        cg->is_64bit ? REG_RAX : REG_EAX);
    asm_jcc_label(a, jump_if_true ? CC_NE : CC_E, lbl);
}


