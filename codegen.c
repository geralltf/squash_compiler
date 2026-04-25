#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* portable strdup replacement */
char* my_strdup(const char* src);

/* forward declarations */
static int sizeof_type_sym(TypeInfo *ti, int is_64, SymTable *sym);
static int expr_has_call(ASTNode *n);

/* =========================================================================
 * resolve_node_type — given an expression node, return bare struct/union type name
 * Handles AST_VAR and nested AST_MEMBER chains recursively.
 * Returns pointer into stable AST/symtable memory (NOT a local buffer), or NULL.
 * ========================================================================= */
static const char *resolve_node_type(SymTable *sym, ASTNode *node) {
    if (!node) return NULL;
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
        /* Look up the parent struct definition */
        char pkey[256]; snprintf(pkey, sizeof pkey, "struct %s", parent_struct);
        Symbol *pss = symtable_lookup(sym, pkey);
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
            if (f->field.type) {
                tfsz = typeinfo_size(f->field.type, sym->is_64bit);
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
                    } else {
                        Symbol *ttd = symtable_lookup(sym, tb2);
                        if (ttd && ttd->kind == SYM_TYPEDEF && ttd->type) {
                            int rs = typeinfo_size(ttd->type, sym->is_64bit);
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
            int talign = elem_tsz < 8 ? elem_tsz : 8;
            if (talign > 1) offset = (offset + talign-1) & ~(talign-1);
            return offset;
        }
        if (!is_union) {
            int fsz = 4; /* default: int-sized */
            if (f->field.type) {
                fsz = typeinfo_size(f->field.type, sym->is_64bit);
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
                        else if (fs && fs->struct_node) fsz = symtable_sizeof_struct(sym, fs->struct_node);
                    } else {
                        /* May be a typedef for a struct */
                        Symbol *td = symtable_lookup(sym, fb);
                        if (td && td->kind == SYM_TYPEDEF && td->type && td->type->pointer_depth == 0) {
                            const char *tb = td->type->base;
                            const char *tbare = tb;
                            if (strncmp(tbare,"struct ",7)==0) tbare+=7;
                            else if (strncmp(tbare,"union ",6)==0) tbare+=6;
                            if (tbare != tb) {
                                char tkey[256]; snprintf(tkey,sizeof tkey,"struct %s",tbare);
                                Symbol *tss = symtable_lookup(sym, tkey);
                                if (tss && tss->struct_size > 0) fsz = tss->struct_size;
                                else if (tss && tss->struct_node) fsz = symtable_sizeof_struct(sym, tss->struct_node);
                            } else {
                                int ts2 = typeinfo_size(td->type, sym->is_64bit);
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
            if (f->field.array_size > 0) fsz *= f->field.array_size;
            /* Align to element's natural alignment (max 8) */
            int align = elem_fsz2 < 8 ? elem_fsz2 : 8;
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
                            if (af->field.array_size>0) fsz*=af->field.array_size;
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
            if (f->field.array_size>0) fsz*=f->field.array_size;
            int align=fsz<8?fsz:8;
            if (align>1) offset=(offset+align-1)&~(align-1);
            offset+=fsz;
        }
    }
    return 0; /* field not found — safe fallback */
}

/* elem_size_of — element size for an array/pointer expression.
 * Handles AST_VAR (stack arrays, pointer vars) and AST_MEMBER (struct field arrays). */
static int elem_size_of(CodeGen *cg, ASTNode *arr_expr) {
    if (!arr_expr) return 4;

    /* Case 1: simple variable — arr[i] or ptr[i] */
    if (arr_expr->kind == AST_VAR) {
        Symbol *asym = symtable_lookup(cg->sym, arr_expr->var.name);
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

/* =========================================================================
 * Internal Win32 function shims — malloc/free/memcpy etc backed by
 * HeapAlloc / HeapFree / kernel routines
 * ========================================================================= */
/* Avoid global pointer arrays (squash codegen can't initialize them).
 * Use explicit strcmp chain instead. */
static int is_internal_shim(const char *name) {
    if (!name) return 0;
    return (strcmp(name,"malloc")==0||strcmp(name,"calloc")==0||strcmp(name,"realloc")==0||strcmp(name,"free")==0||
            strcmp(name,"memcpy")==0||strcmp(name,"memset")==0||strcmp(name,"memcmp")==0||strcmp(name,"memmove")==0||
            strcmp(name,"strlen")==0||strcmp(name,"strcpy")==0||strcmp(name,"strncpy")==0||strcmp(name,"strcmp")==0||
            strcmp(name,"strncmp")==0||strcmp(name,"strcat")==0||strcmp(name,"strncat")==0||
            strcmp(name,"strchr")==0||strcmp(name,"strrchr")==0||strcmp(name,"strstr")==0||
            strcmp(name,"strpbrk")==0||strcmp(name,"strtok")==0||
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

/* =========================================================================
 * Init
 * ========================================================================= */
void codegen_init(CodeGen *cg, Assembler *a, SymTable *sym, int is_64bit) {
    memset(cg,0,sizeof *cg);
    cg->asm_=a; cg->sym=sym; cg->is_64bit=is_64bit;
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
static const char *intern_string(CodeGen *cg, const char *value) {
    for (int i=0;i<cg->string_count;i++) {
        if (cg->strings[i].len == 8) continue; /* skip float constant entries */
        if (strcmp(cg->strings[i].value,value)==0)
            return cg->strings[i].label;
    }
    if (cg->string_count==cg->string_cap) {
        cg->string_cap*=2;
        cg->strings=realloc(cg->strings,cg->string_cap*sizeof(StringEntry));
    }
    StringEntry *se=&cg->strings[cg->string_count++];
    se->value  = my_strdup(value);
    se->len    = (int)strlen(value)+1;
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
    we->label  = my_strdup(label);
    we->offset = cg->wdata_pool_size;
    we->size   = size;
    cg->wdata_count++;
    cg->wdata_pool_size += size;
    /* Align to 8 bytes */
    int pad = (8 - (cg->wdata_pool_size & 7)) & 7;
    cg->wdata_pool_size += pad;
    return we->label;
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
            return sizeof_type_sym(td->type, is_64, sym);
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



/* =========================================================================
 * Internal shim dispatch
 * ========================================================================= */
static void emit_internal_call(CodeGen *cg, const char *name, ASTNode **args, int argc) {
    Assembler *a = cg->asm_;

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
                    if(af) codegen_float_expr(cg,args[i]);
                    else { codegen_expr(cg,args[i]); asm_mov_reg_reg(a,ARG_REG(i),REG_RAX); }
                    if (this_has_call) {
                        for (int k=0; k<i; k++)
                            asm_mov_reg_reg(a, ARG_REG(k), SAVE_REG(k));
                    }
                }
#undef ARG_REG
#undef SAVE_REG
                /* Extra args on stack (args 4+): need shadow first */
                int extra=(argc>4)?(argc-4)*8:0;
                int frame=32+extra; if((frame&8)==0) frame+=8;
                asm_sub_rsp(a,frame);
                for(int i=4;i<argc;i++){
                    int af=codegen_is_float_expr(cg,args[i]);
                    if(af){codegen_float_expr(cg,args[i]);asm_movsd_store(a,REG_RSP,32+(i-4)*8,0);}
                    else{codegen_expr(cg,args[i]);asm_mov_mem_reg(a,REG_RSP,32+(i-4)*8,REG_RAX);}
                }
                asm_call_import(a,name);
                asm_add_rsp(a,frame);
            } else {
                /* 32-bit cdecl: push args right-to-left */
                int total=0;
                for(int i=argc-1;i>=0;i--){
                    int af=codegen_is_float_expr(cg,args[i]);
                    if(af){codegen_float_expr(cg,args[i]);asm_sub_rsp(a,8);asm_fstp_mem64(a,REG_ESP,0);total+=8;}
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
            /* 32-bit stdcall HeapReAlloc(heap, flags, ptr, size) */
            asm_push_reg(a, REG_EAX);               /* save ptr */
            if (argc >= 2) codegen_expr(cg, args[1]);
            else           asm_mov_reg_imm(a, REG_RAX, 0);
            asm_push_reg(a, REG_EAX);               /* save size */
            asm_push_imm32(a, 0);                   /* placeholder */
            asm_call_import32(a, "GetProcessHeap");
            asm_add_rsp(a, 4);                      /* discard placeholder (keep EBX clean) */
            /* stack: [size, ptr], EAX = heap */
            asm_pop_reg(a, REG_ECX);                /* ECX = size */
            asm_pop_reg(a, REG_EDX);                /* EDX = ptr */
            /* push stdcall args right-to-left: heap, flags, ptr, size */
            asm_push_reg(a, REG_ECX);               /* push size */
            asm_push_reg(a, REG_EDX);               /* push ptr */
            asm_push_imm32(a, 0);                   /* push flags=0 */
            asm_push_reg(a, REG_EAX);               /* push heap */
            asm_call_import32(a, "HeapReAlloc");
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
            int extra = (argc > 4) ? (argc-4)*8 : 0;
            int frame = 32 + extra; if ((frame&8)==0) frame+=8;
            asm_sub_rsp(a2, frame);
            for (int i=0; i<argc; i++) {
                codegen_expr(cg, args[i]);
                if      (i==0) asm_mov_reg_reg(a2,REG_RCX,REG_RAX);
                else if (i==1) asm_mov_reg_reg(a2,REG_RDX,REG_RAX);
                else if (i==2) asm_mov_reg_reg(a2,REG_R8, REG_RAX);
                else if (i==3) asm_mov_reg_reg(a2,REG_R9, REG_RAX);
                else           asm_mov_mem_reg(a2,REG_RSP,32+(i-4)*8,REG_RAX);
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
            int extra = (argc > 4) ? (argc-4)*8 : 0;
            int frame = 32 + extra; if ((frame&8)==0) frame+=8;
            asm_sub_rsp(a2, frame);
            for (int i=0; i<argc; i++) {
                codegen_expr(cg, args[i]);
                if      (i==0) asm_mov_reg_reg(a2,REG_RCX,REG_RAX);
                else if (i==1) asm_mov_reg_reg(a2,REG_RDX,REG_RAX);
                else if (i==2) asm_mov_reg_reg(a2,REG_R8, REG_RAX);
                else if (i==3) asm_mov_reg_reg(a2,REG_R9, REG_RAX);
                else           asm_mov_mem_reg(a2,REG_RSP,32+(i-4)*8,REG_RAX);
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
        if (!s) { printf("codegen_lvalue: undefined '%s'\n",n->var.name); asm_mov_reg_imm(a,REG_RAX,0); return; }
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

/* =========================================================================
 * codegen_expr — evaluate expr, result in RAX/EAX
 * ========================================================================= */
void codegen_expr(CodeGen *cg, ASTNode *n) {
    Assembler *a=cg->asm_;
    if (!n) { asm_mov_reg_imm(a,REG_RAX,0); return; }
    printf("[CE] kind=%d\n",n->kind); fflush(0);

    /* Float dispatch: route float arithmetic through SSE2/x87 codegen.
     * Only pure-value nodes: AST_FLOAT literal, AST_BINARY, AST_UNARY, AST_CAST.
     * AST_ASSIGN and AST_VAR are handled below to avoid mutual recursion.    */
    if (n->kind == AST_FLOAT) { codegen_float_expr(cg,n); return; }
    if ((n->kind==AST_BINARY || n->kind==AST_UNARY || n->kind==AST_CAST)
        && codegen_is_float_expr(cg,n)) {
        codegen_float_expr(cg,n);
        /* Move float result to RAX as raw bits for comparison/branch context */
        if (cg->is_64bit) { asm_emit4(a,0x66,0x48,0x0F,0x7E); asm_emit1(a,0xC0); }
        return;
    }

    switch (n->kind) {
    case AST_NUMBER:   asm_mov_reg_imm(a,REG_RAX,n->num.value); break;
    case AST_FLOAT:    codegen_float_expr(cg,n); return; /* handled above but fallback */
    case AST_CHAR_LIT: asm_mov_reg_imm(a,REG_RAX,n->char_lit.value); break;

    case AST_STRING: {
        const char *lbl=intern_string(cg,n->str.value);
        if (cg->is_64bit) asm_lea_rip_data(a,REG_RAX,lbl);
        else { asm_emit1(a,0xB8); asm_reloc_data(a,lbl); }
        break;
    }

    case AST_VAR: {
        Symbol *s=symtable_lookup(cg->sym,n->var.name);
        if (!s) { printf("codegen: undefined '%s' at line %d\n",n->var.name,n->line); asm_mov_reg_imm(a,REG_RAX,0); return; }
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
                int vsz = s->type ? typeinfo_size(s->type, 1) : 4;
                /* Use TypeInfo.is_unsigned field: unsigned int/char/bool -> zero-extend,
                 * signed int/char/etc -> sign-extend for correct comparisons */
                int is_signed = !(s->type && s->type->is_unsigned);
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
                if (!is_array) asm_emit2(a,0x8B,0x00); /* mov eax,[rax] */
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
             * function pointer variable: int (*op)(int,int) = fp_add;     */
            int func_lbl = get_func_label(cg, n->var.name);
            asm_load_func_addr(a, REG_RAX, func_lbl);
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
                if (strcmp(base,"char")==0 || strcmp(base,"signed char")==0) {
                    /* movsx eax,al */
                    if (cg->is_64bit) asm_emit4(a,0x48,0x0F,0xBE,0xC0);
                    else              asm_emit3(a,0x0F,0xBE,0xC0);
                } else if (strcmp(base,"unsigned char")==0) {
                    /* movzx eax,al */
                    asm_emit3(a,0x0F,0xB6,0xC0);
                } else if (strcmp(base,"short")==0 || strcmp(base,"signed short")==0) {
                    /* movsx eax,ax */
                    if (cg->is_64bit) asm_emit4(a,0x48,0x0F,0xBF,0xC0);
                    else              asm_emit3(a,0x0F,0xBF,0xC0);
                } else if (strcmp(base,"unsigned short")==0) {
                    /* movzx eax,ax */
                    asm_emit3(a,0x0F,0xB7,0xC0);
                } else if (!is_unsigned && strcmp(base,"char")==0) {
                    /* default char: treat as signed */
                    if (cg->is_64bit) asm_emit4(a,0x48,0x0F,0xBE,0xC0);
                    else              asm_emit3(a,0x0F,0xBE,0xC0);
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
        /* Determine load size from the operand's symbol type */
        if (n->deref.operand && n->deref.operand->kind==AST_VAR) {
            Symbol *ds=symtable_lookup(cg->sym,n->deref.operand->var.name);
            if (ds && ds->type && ds->type->pointer_depth>=1) {
                const char *base=ds->type->base;
                if (base && (strcmp(base,"char")==0||strcmp(base,"signed char")==0||
                             strcmp(base,"unsigned char")==0)) load1=1;
                else if (base && (strcmp(base,"short")==0||strcmp(base,"unsigned short")==0)) load2=1;
                else if (ds->type->pointer_depth>=2) load8=cg->is_64bit;
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
            if (!cg->is_64bit) asm_emit2(a,0x8B,0x00); /* mov eax,[eax] */
            else               asm_emit2(a,0x8B,0x00); /* mov eax,[rax] zero-extends */
        }
        break;
    }

    case AST_INDEX: {
        if (!n->index.array || !n->index.index) { asm_mov_reg_imm(a,REG_RAX,0); break; }
        int esz = elem_size_of(cg, n->index.array);
        codegen_lvalue(cg, n);          /* RAX/EAX = address of element */
        /* Load element using appropriate width */
        if (esz == 8 && cg->is_64bit) {
            asm_emit3(a,0x48,0x8B,0x00); /* mov rax,[rax]          64-bit     */
        } else if (esz == 1) {
            /* movzx eax,byte[rax] — zero-extend byte to int                  */
            if (cg->is_64bit) asm_emit3(a,0x0F,0xB6,0x00);
            else              asm_emit3(a,0x0F,0xB6,0x00);
        } else if (esz == 2) {
            /* movzx eax,word[rax]                                            */
            if (cg->is_64bit) asm_emit3(a,0x0F,0xB7,0x00);
            else              asm_emit3(a,0x0F,0xB7,0x00);
        } else {
            asm_emit2(a,0x8B,0x00);      /* mov eax,[rax]          32-bit     */
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
                  if (ss && ss->struct_node) {
                      for (int _i=0;_i<ss->struct_node->struct_decl.nfields;_i++) {
                          ASTNode *ff=ss->struct_node->struct_decl.fields[_i];
                          if (ff&&ff->field.name&&strcmp(ff->field.name,mfname)==0&&ff->field.type) {
                              if (ff->field.type->pointer_depth > 0) load64 = cg->is_64bit ? 1 : 0;
                              /* Fixed-size array field: array decays to pointer to first element.
                               * Return the address of the field instead of loading its value.
                               * IMPORTANT: Use upper-bound guard < 0x40000000 to prevent false positives
                               * when squash zero-extends int field -1 to 4294967295. */
                              if ((ff->field.type->array_size > 0 && ff->field.type->array_size < 0x40000000) ||
                                  (ff->field.array_size > 0 && ff->field.array_size < 0x40000000))
                                  is_array_field = 1;
                              field_is_unsigned = ff->field.type->is_unsigned;
                              field_sz = typeinfo_size(ff->field.type, cg->is_64bit);
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
              /* 64-bit mode: use movsxd for signed 4-byte, mov rax for 8-byte */
              if (field_found && !field_is_unsigned && field_sz == 4) {
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
        /* Float assignment: if RHS is float type, use SSE2/x87 path */
        /* Float assignment: handle "=" and compound "+=","-=","*=","/=" for float vars */
        {
            int rhs_is_float = codegen_is_float_expr(cg, n->assign.rhs);
            int lhs_is_float_var = 0;
            Symbol *fsv = NULL;
            if (n->assign.lhs->kind==AST_VAR) {
                fsv = symtable_lookup(cg->sym, n->assign.lhs->var.name);
                if (fsv && fsv->type) lhs_is_float_var = typeinfo_is_float(fsv->type);
            }
            if (lhs_is_float_var || (strcmp(op,"=")==0 && rhs_is_float)) {
                int is_compound = (strcmp(op,"=")==0) ? 0 : 1;
                if (is_compound && fsv && cg->is_64bit &&
                    (fsv->kind==SYM_VAR||fsv->kind==SYM_PARAM)) {
                    /* Spill old lhs value onto stack (NOT XMM1, which rhs eval may clobber) */
                    asm_movsd_load(a,0,REG_RBP,fsv->offset); /* XMM0 = old lhs */
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
                    if (cg->is_64bit){asm_lea_rip_wdata(a,REG_RBX,lbl);asm_movsd_store(a,REG_RBX,0,0);}
                } else if (!fsv && cg->is_64bit &&
                           (n->assign.lhs->kind==AST_MEMBER ||
                            n->assign.lhs->kind==AST_INDEX  ||
                            n->assign.lhs->kind==AST_DEREF)) {
                    /* LHS is a member/array/deref: compute address into RAX.
                     * codegen_lvalue uses only integer registers (RAX/RBX/etc), so XMM0
                     * is preserved across the call. Then store with movsd [rax],xmm0. */
                    codegen_lvalue(cg, n->assign.lhs); /* RAX = address of LHS */
                    /* movsd [RAX], xmm0  —  F2 0F 11 00 */
                    asm_emit4(a,0xF2,0x0F,0x11,0x00);
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
            else if (strcmp(op,"/=")==0)  { asm_mov_reg_reg(a,REG_RCX,REG_RBX); asm_idiv_reg(a,REG_RCX); }
            else if (strcmp(op,"%=")==0)  { asm_mov_reg_reg(a,REG_RCX,REG_RBX); asm_idiv_reg(a,REG_RCX); asm_mov_reg_reg(a,REG_RAX,REG_RDX); }
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
                    int vsz = typeinfo_size(s->type, 1);
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
                asm_emit2(a,0x89,0x03);  /* mov [rbx/ebx], eax */
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
                                    store_sz = typeinfo_size(ff->field.type, 1);
                                    if (ff->field.type->pointer_depth > 0) store_sz = 8;
                                    break;
                                }
                            }
                        }
                    }
                } else if (cg->is_64bit && lhs->kind == AST_VAR) {
                    Symbol *sv = symtable_lookup(cg->sym, lhs->var.name);
                    if (sv && sv->type && sv->type->pointer_depth > 0) store_sz = 8;
                } else if (cg->is_64bit && lhs->kind == AST_DEREF) {
                    /* *ptr = val — width from ptr's base type */
                    ASTNode *op = lhs->deref.operand;
                    if (op && op->kind == AST_VAR) {
                        Symbol *sv = symtable_lookup(cg->sym, op->var.name);
                        if (sv && sv->type && sv->type->pointer_depth > 1) store_sz = 8;
                        else if (sv && sv->type && sv->type->pointer_depth==1) {
                            /* *charptr = val — 1 byte */
                            const char *b2=sv->type->base;
                            if (strncmp(b2,"unsigned ",9)==0) b2+=9;
                            if (strcmp(b2,"char")==0||strcmp(b2,"int8_t")==0||strcmp(b2,"uint8_t")==0)
                                store_sz=1;
                        }
                    }
                }
            }
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
        if (n->unary.post) {
            /* post ++ / -- */
            codegen_expr(cg,n->unary.operand);
            asm_push_reg(a,REG_RAX); /* save original */
            if (strcmp(op,"++")==0) {
                asm_mov_reg_imm(a,REG_RBX,1);
                asm_add_reg_reg(a,REG_RAX,REG_RBX);
            } else {
                asm_mov_reg_imm(a,REG_RBX,1);
                asm_sub_reg_reg(a,REG_RAX,REG_RBX);
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
                    asm_emit2(a,0x89,0x03); /* mov [rbx/ebx],eax */
                }
            } else if (n->unary.operand->kind==AST_MEMBER ||
                       n->unary.operand->kind==AST_INDEX  ||
                       n->unary.operand->kind==AST_DEREF) {
                /* Store incremented value back to struct/array/deref lvalue */
                asm_push_reg(a,REG_RAX); /* save incremented value */
                codegen_lvalue(cg,n->unary.operand);
                asm_pop_reg(a,REG_RBX); /* incremented value */
                asm_emit2(a,0x89,0x18); /* mov [rax],ebx (32-bit store — ints/chars) */
            }
            asm_pop_reg(a,REG_RAX); /* return original value */
        } else if (strcmp(op,"++")==0||strcmp(op,"--")==0) {
            codegen_expr(cg,n->unary.operand);
            asm_mov_reg_imm(a,REG_RBX,1);
            if (strcmp(op,"++")==0) asm_add_reg_reg(a,REG_RAX,REG_RBX);
            else asm_sub_reg_reg(a,REG_RAX,REG_RBX);
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
                    asm_emit2(a,0x89,0x03); /* mov [rbx/ebx],eax */
                }
            } else if (n->unary.operand->kind==AST_MEMBER ||
                       n->unary.operand->kind==AST_INDEX  ||
                       n->unary.operand->kind==AST_DEREF) {
                /* Store incremented value back to struct/array/deref lvalue */
                asm_push_reg(a,REG_RAX); /* save incremented value */
                codegen_lvalue(cg,n->unary.operand);
                asm_pop_reg(a,REG_RBX); /* incremented value */
                asm_emit2(a,0x89,0x18); /* mov [rax],ebx */
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
        codegen_expr(cg,n->binary.left);  asm_push_reg(a,REG_RAX);
        codegen_expr(cg,n->binary.right); asm_pop_reg(a,REG_RBX);
        /* RBX=left, RAX=right */
        if      (strcmp(op,"+")==0)  { asm_add_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"-")==0)  { asm_sub_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"*")==0)  { asm_imul_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"/")==0)  { asm_mov_reg_reg(a,REG_RCX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); asm_idiv_reg(a,REG_RCX); }
        else if (strcmp(op,"%")==0)  { asm_mov_reg_reg(a,REG_RCX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); asm_idiv_reg(a,REG_RCX); asm_mov_reg_reg(a,REG_RAX,REG_RDX); }
        else if (strcmp(op,"&")==0)  { asm_and_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"|")==0)  { asm_or_reg_reg (a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"^")==0)  { asm_xor_reg_reg(a,REG_RBX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); }
        else if (strcmp(op,"<<")==0) { asm_mov_reg_reg(a,REG_RCX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); asm_shl_reg_cl(a,REG_RAX); }
        else if (strcmp(op,">>")==0) { asm_mov_reg_reg(a,REG_RCX,REG_RAX); asm_mov_reg_reg(a,REG_RAX,REG_RBX); asm_shr_reg_cl(a,REG_RAX); }
        else {
            asm_cmp_reg_reg(a,REG_RBX,REG_RAX);
            CondCode cc;
            if      (strcmp(op,"==")==0) cc=CC_E;
            else if (strcmp(op,"!=")==0) cc=CC_NE;
            else if (strcmp(op,"<" )==0) cc=CC_L;
            else if (strcmp(op,">" )==0) cc=CC_G;
            else if (strcmp(op,"<=")==0) cc=CC_LE;
            else                          cc=CC_GE;
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

        if (is_internal_shim(name)) {
            /* shim_uses_regs: string/memory shims that clobber ESI/EDI/EBX in 32-bit */
                        if (!cg->is_64bit && (!strcmp(name,"strcpy")||!strcmp(name,"strcmp")||!strcmp(name,"strncmp")||                !strcmp(name,"strcat")||!strcmp(name,"strncat")||!strcmp(name,"strchr")||!strcmp(name,"strstr")||                !strcmp(name,"strncpy")||!strcmp(name,"atoi")||!strcmp(name,"atol")||!strcmp(name,"memcmp")||                !strcmp(name,"memcpy")||!strcmp(name,"memmove")||!strcmp(name,"memset"))) {
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

        /* If the symbol is a local variable or parameter (not a declared function),
         * treat it as a function pointer: load the pointer value and call through it.
         * This handles: int (*op)(int,int); op = fp_add; op(1,2);              */
        int is_fptr_var = (sym &&
                          (sym->kind==SYM_VAR || sym->kind==SYM_PARAM) &&
                           sym->type && sym->type->pointer_depth > 0);
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

        int argc=n->call.argc;
        if (cg->is_64bit) {
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
                        if(af){codegen_float_expr(cg,n->call.args[0]);}
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
                            codegen_float_expr(cg,n->call.args[1]);
                            /* Win64 ABI: float at position 1 must be in XMM1, not XMM0 */
                            asm_movsd_xmm(a,1,0); /* movsd xmm1, xmm0 */
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
                            codegen_float_expr(cg,n->call.args[2]);
                            /* Win64 ABI: float at position 2 must be in XMM2 */
                            asm_movsd_xmm(a,2,0); /* movsd xmm2, xmm0 */
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
                            codegen_float_expr(cg,n->call.args[3]);
                            /* Win64 ABI: float at position 3 must be in XMM3 */
                            asm_movsd_xmm(a,3,0); /* movsd xmm3, xmm0 */
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
                if(af){codegen_float_expr(cg,n->call.args[i]); asm_movsd_store(a,REG_RSP,32+(i-4)*8,0);}
                else{codegen_expr(cg,n->call.args[i]); asm_mov_mem_reg(a,REG_RSP,32+(i-4)*8,REG_RAX);}
            }
            if (sym && sym->kind==SYM_IMPORT) {
                char key[512]; snprintf(key,sizeof key,"%s:%s",sym->dll,name);
                symtable_add_import(cg->sym,key);
                asm_call_import(a,name);
            } else if (sym && sym->kind==SYM_FUNC) {
                /* Check if it's a Windows API (declared extern but really an import) */
                const char *dll64 = symtable_find_dll(cg->sym, name);
                if (dll64) {
                    char key64[512]; snprintf(key64,sizeof key64,"%s:%s",dll64,name);
                    symtable_add_import(cg->sym,key64);
                    asm_call_import(a,name);
                } else {
                    /* Locally defined function: call directly */
                    asm_call_direct(a,get_func_label(cg,name));
                }
            } else {
                /* Unknown function: try msvcrt.dll passthrough for C stdlib */
                char key[512]; snprintf(key,sizeof key,"msvcrt.dll:%s",name);
                symtable_add_import(cg->sym,key);
                asm_call_import(a,name);
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
                } else {
                    codegen_expr(cg, n->call.args[i]);
                    asm_push_reg(a, REG_EAX);
                    total_arg_bytes += 4;
                }
            }
            if (sym && sym->kind==SYM_IMPORT) {
                char key[512]; snprintf(key,sizeof key,"%s:%s",sym->dll,name);
                symtable_add_import(cg->sym,key);
                asm_call_import32(a,name);
            } else if (sym && sym->kind==SYM_FUNC) {
                const char *dll32 = symtable_find_dll(cg->sym, name);
                if (dll32) {
                    char key32b[512]; snprintf(key32b,sizeof key32b,"%s:%s",dll32,name);
                    symtable_add_import(cg->sym,key32b);
                    asm_call_import32(a,name);
                } else {
                    asm_call_direct(a,get_func_label(cg,name));
                }
            } else {
                /* Unknown function: msvcrt passthrough */
                char key32[512]; snprintf(key32,sizeof key32,"msvcrt.dll:%s",name);
                symtable_add_import(cg->sym,key32);
                asm_call_import32(a,name);
            }
            if (total_arg_bytes>0) asm_add_rsp(a,total_arg_bytes);
        }
        break;
    }

    case AST_FUNC_PTR_CALL: {
        if (!n->fp_call.func_expr) { asm_mov_reg_imm(a,REG_RAX,0); break; }
        int argc = n->fp_call.argc;
        if (cg->is_64bit) {
            int extra = (argc > 4) ? (argc-4)*8 : 0;
            int frame = 32 + extra;
            if ((frame & 8) == 0) frame += 8;  /* keep RSP 16-aligned at CALL */
            asm_sub_rsp(a, frame);
            /* Evaluate the function pointer expression into RAX, save to RBX */
            codegen_expr(cg, n->fp_call.func_expr);
            asm_mov_reg_reg(a, REG_RBX, REG_RAX);
            /* Evaluate and place each argument */
            for (int i = 0; i < argc; i++) {
                codegen_expr(cg, n->fp_call.args[i]);
                if      (i == 0) asm_mov_reg_reg(a, REG_RCX, REG_RAX);
                else if (i == 1) asm_mov_reg_reg(a, REG_RDX, REG_RAX);
                else if (i == 2) asm_mov_reg_reg(a, REG_R8,  REG_RAX);
                else if (i == 3) asm_mov_reg_reg(a, REG_R9,  REG_RAX);
                else             asm_mov_mem_reg(a, REG_RSP, 32+(i-4)*8, REG_RAX);
            }
            asm_call_reg(a, REG_RBX);
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
 * codegen_stmt
 * ========================================================================= */
void codegen_stmt(CodeGen *cg, ASTNode *n) {
    Assembler *a=cg->asm_;
    if (!n) return;
    printf("[CS] kind=%d\n",n->kind); fflush(0);
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
        codegen_expr(cg,n->expr_stmt.expr);
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
            int elem_sz = n->var_decl.type ?
                typeinfo_size(n->var_decl.type, cg->is_64bit) : 4;
            if (elem_sz < 1) elem_sz = 4;
            int total_sz = (arr > 0) ? elem_sz * arr : elem_sz;

            /* Unique mangled label */
            char sym_name[256];
            snprintf(sym_name, sizeof sym_name, "static_%s_%d",
                     n->var_decl.name, cg->string_count);

            /* Allocate a zero-filled buffer — DO NOT free, StringEntry owns it */
            char *zbuf = calloc(total_sz + 1, 1);  /* +1 so strlen is safe   */

             /* Register in writable data pool (.data section, R/W).
              * Static vars MUST be writable — they store live state across calls. */
             intern_wdata(cg, sym_name, total_sz);
             free(zbuf);  /* wdata doesn't need the buffer content               */

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
        int is_float_var = typeinfo_is_float(effective_type);
        if (is_float_var) {
            /* Allocate 8-byte slot for double, 4-byte for float */
            int fsz = effective_typeinfo_size(cg, effective_type, cg->is_64bit);
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
            if (n->var_decl.init->kind==AST_BLOCK && arr>0) {
                /* array initialiser: store each element at its natural element size */
                int esz = n->var_decl.type ? typeinfo_size(n->var_decl.type, cg->is_64bit) : 4;
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
        asm_push_reg(a,REG_RAX); /* save switch value */
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
        asm_pop_reg(a,REG_RAX); /* pop saved switch value */
        cg->switch_end_label=sv_sw; cg->loop_end_label=sv_end;
        break;
    }

    case AST_BREAK:
        if (cg->loop_end_label>=0) asm_jmp_label(a,cg->loop_end_label);
        else printf("codegen: break outside loop/switch\n");
        break;

    case AST_CONTINUE:
        if (cg->loop_top_label>=0) asm_jmp_label(a,cg->loop_top_label);
        else printf("codegen: continue outside loop\n");
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
        printf("[RET] ret.expr=%p\n",(void*)n->ret.expr); fflush(0);
        if (n->ret.expr) {
            printf("[RET2] kind=%d\n",n->ret.expr->kind); fflush(0);
            if (codegen_is_float_expr(cg, n->ret.expr)) {
                /* Float-returning function: result must be in XMM0 (64-bit)
                 * or ST0 (32-bit x87). */
                codegen_float_expr(cg, n->ret.expr);
            } else {
                codegen_expr(cg, n->ret.expr);
            }
        } else {
            asm_mov_reg_imm(a, REG_RAX, 0);
        }
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

    default:
        printf("codegen_stmt: unhandled node %d\n",n->kind);
    }
}

/* =========================================================================
 * codegen_func
 * ========================================================================= */
void codegen_func(CodeGen *cg, ASTNode *n) {
    Assembler *a=cg->asm_;
    if (!n||n->kind!=AST_FUNC_DECL) return;
    if (!n->func.body) return; /* forward declaration */

    printf("[CGF] %s\n",n->func.name); fflush(0);
    int lid=get_func_label(cg,n->func.name);
    asm_def_label(a,lid);

    symtable_push_scope(cg->sym);
    symtable_reset_locals(cg->sym);
    cg->sym->next_offset=0;

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

    /* Two-pass frame sizing: emit prologue with placeholder size=0,
     * compile the body (which defines locals in the symtable),
     * then patch the placeholder with the actual aligned frame size.
     * This eliminates the hardcoded sub rsp,0x108 that triggers AV. */
    int frame_patch = asm_enter_deferred(a, cg->chkstk_lbl); /* probe for both 32 and 64-bit */

    /* Spill register params FIRST — before CRT startup calls which clobber registers */
    if (cg->is_64bit) {
        /* Avoid static Reg arrays (squash can't init non-zero local statics). */
#define PR_REG(i) ((i)==0?REG_RCX:(i)==1?REG_RDX:(i)==2?REG_R8:REG_R9)
        for (int i=0;i<n->func.paramc&&i<4;i++) {
            ASTNode *pr=n->func.params[i];
            int is_float_param = pr->param.type && typeinfo_is_float(pr->param.type);
            if (is_float_param) {
                /* Float param arrives in XMMi — spill with movsd [rbp+16+i*8],xmmi */
                asm_movsd_store(a, REG_RBP, 16+i*8, i); /* movsd [rbp+slot],xmmi */
            } else {
                asm_mov_mem_reg(a,REG_RBP,16+i*8,PR_REG(i));
            }
        }
#undef PR_REG
    }
    /* stdout via msvcrt printf - no GetStdHandle/WriteFile needed */
    /* If main() has argc/argv params, populate them from GetCommandLineA */
    if (strcmp(n->func.name,"main")==0 && n->func.paramc >= 1) {
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
    printf("[CGF_BODY] %s\n",n->func.name); fflush(0);
    codegen_stmt(cg,n->func.body);
    printf("[CGF_DONE] %s\n",n->func.name); fflush(0);

    /* Default return 0 at fall-through */
    asm_mov_reg_imm(a,REG_RAX,0);
    asm_leave(a); asm_ret(a);

    /* Patch the frame size with the actual number of bytes needed.
     * Round up to 16, then add 8 so RSP stays 16-byte aligned after PUSH RBP. */
    int raw   = symtable_local_size(cg->sym);
    int align = (raw + 15) & ~15;
    if (cg->is_64bit && (align & 8) == 0) align += 8;
    if (align < 8) align = 8;   /* minimum: at least 8 bytes for alignment */
    /* Reserve 16 extra bytes for float binary spill slots (sub rsp,16 in codegen_float_expr) */
    if (cg->is_64bit && align < 24) align = 24;
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
        int vsz = vtype ? typeinfo_size(vtype, cg->is_64bit) : 4;
        if (vsz < 1) vsz = 4;
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
        printf("[P2] i=%d count=%d d=%p kind=%d\n",i,prog->program.count,(void*)d,d?d->kind:-1); fflush(0);
        if (!d) continue;
        if (d->kind==AST_FUNC_DECL && d->func.body!=NULL) {
            codegen_func(cg,d);
        }
        /* (Global VAR_DECLs handled in Pass 0 above) */
    }
    printf("[CG_RESOLVE] calling asm_resolve label_count=%d\n",cg->asm_->label_count); fflush(0);
    asm_resolve(cg->asm_);
    printf("[CG_RESOLVE_DONE]\n"); fflush(0);
}

uint8_t    *codegen_get_text  (CodeGen *cg, int *len) { *len=cg->asm_->code_len; return cg->asm_->code; }
uint8_t    *codegen_get_rdata (CodeGen *cg, int *len) { return build_rdata(cg,len); }
Relocation *codegen_get_relocs(CodeGen *cg, int *cnt) { *cnt=cg->asm_->reloc_count; return cg->asm_->relocs; }

/* =========================================================================
 * Float constant pool — stores double literals in .rdata for RIP-relative access
 * ========================================================================= */
const char *intern_float_const(CodeGen *cg, double val) {
    /* Store float constant directly in the string pool (.rdata) so that
     * RELOC_DATA_REL32 / RELOC_DATA_ABS32 can resolve it via string_labels[]. */
    uint64_t bits; memcpy(&bits, &val, 8);
    /* Check if already interned (scan string pool for matching 8-byte double) */
    for (int i = 0; i < cg->string_count; i++) {
        if (cg->strings[i].len == 8) {
            uint64_t eb; memcpy(&eb, cg->strings[i].value, 8);
            if (eb == bits) return cg->strings[i].label;
        }
    }
    /* Allocate 8-byte entry in string pool */
    if (cg->string_count == cg->string_cap) {
        cg->string_cap *= 2;
        cg->strings = realloc(cg->strings, cg->string_cap * sizeof(StringEntry));
    }
    /* 8-byte aligned offset in pool */
    int off = (cg->string_pool_size + 7) & ~7;
    cg->string_pool_size = off + 8;

    char *buf = malloc(8); memcpy(buf, &val, 8);
    char *lbl = malloc(32);
    snprintf(lbl, 32, "fconst_%d", cg->string_count);

    StringEntry *se = &cg->strings[cg->string_count++];
    se->value  = buf;
    se->len    = 8;
    se->offset = off;
    se->label  = lbl;
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

int codegen_is_float_expr(CodeGen *cg, ASTNode *n) {
    if (!n) return 0;
    switch (n->kind) {
    case AST_FLOAT: return 1;
    case AST_NUMBER: return 0;
    case AST_VAR: {
        Symbol *s = symtable_lookup(cg->sym, n->var.name);
        if (s && s->type) return is_float_type(cg, s->type);
        return 0;
    }
    case AST_CAST: return n->cast.type && is_float_type(cg, n->cast.type);
    case AST_BINARY: {
        printf("[FLT] binary op=%c%c left=%p right=%p\n",n->binary.op[0],n->binary.op[1],(void*)n->binary.left,(void*)n->binary.right); fflush(0);
        if (n->binary.left)  { printf("[FLT_L] left->kind=%d\n",n->binary.left->kind);  fflush(0); }
        if (n->binary.right) { printf("[FLT_R] right->kind=%d\n",n->binary.right->kind); fflush(0); }
        printf("[FLT_OP] op addr=%p op0=%d\n",(void*)n->binary.op,(int)n->binary.op[0]); fflush(0);
        const char *op = n->binary.op;
        printf("[FLT_STRCMP] op=%s\n",op); fflush(0);
        /* Comparison ops always return int (0/1), even if operands are float */
        int is_cmp = (!strcmp(op,"==")||!strcmp(op,"!=")||!strcmp(op,"<")||
                     !strcmp(op,"<=")||!strcmp(op,">")||!strcmp(op,">="));
        printf("[FLT_ISCMP] is_cmp=%d\n",is_cmp); fflush(0);
        if (is_cmp) return 0;
        /* For arithmetic ops: if either operand is float, result is float */
        printf("[FLT2] going recursive left=%p right=%p\n",(void*)n->binary.left,(void*)n->binary.right); fflush(0);
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
    case AST_TERNARY:
        return codegen_is_float_expr(cg, n->ternary.then_) ||
               codegen_is_float_expr(cg, n->ternary.else_);
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
        /* Literal float/double constant.
         * Copy to a local double first so squash-compiled callers pass it
         * through the float register path (XMM1) rather than the integer path. */
        double fval = n->fnum.value;
        const char *lbl = intern_float_const(cg, fval);
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
        const char *lbl = intern_float_const(cg, v);
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
                const char *nlbl = intern_float_const(cg, neg);
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
            /* float<->double conversion */
            if (t && strcmp(t->base,"float")==0 && cg->is_64bit) asm_cvtsd2ss(a,0,0);
            else if (t && strcmp(t->base,"double")==0 && cg->is_64bit) asm_cvtss2sd(a,0,0);
            /* else stays as is */
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
                    asm_lea_rip_wdata(a,REG_RBX,lbl);
                    asm_movsd_store(a,REG_RBX,0,0);
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
        /* Float/double struct field: get base address, then movsd to load double */
        if (n->member.arrow) codegen_expr(cg, n->member.obj);
        else codegen_lvalue(cg, n->member.obj);
        int foff = field_byte_offset(cg->sym, n->member.obj, n->member.field);
        if (cg->is_64bit) asm_movsd_load(a, 0, REG_RAX, foff);
        else asm_fld_mem64(a, REG_EAX, foff);
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
    return buf; /* zero-initialized wdata */
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
    if (n->kind == AST_CALL) return 1;
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
            /* Determine Jcc: jump to lbl when condition is FALSE (for if/while) */
            CondCode cc;
            if      (!strcmp(op,"==")) cc = jump_if_true ? CC_E  : CC_NE;
            else if (!strcmp(op,"!=")) cc = jump_if_true ? CC_NE : CC_E;
            else if (!strcmp(op,"<"))  cc = jump_if_true ? CC_L  : CC_GE;
            else if (!strcmp(op,"<=")) cc = jump_if_true ? CC_LE : CC_G;
            else if (!strcmp(op,">"))  cc = jump_if_true ? CC_G  : CC_LE;
            else                       cc = jump_if_true ? CC_GE : CC_L;
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


